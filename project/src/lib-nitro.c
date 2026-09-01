// Nintendo DS ("Nitro") sprite compositing -- see lib-nitro.h.

#include "lib-std.h"
#include "lib-nitro.h"

static inline u16 nrd16 (const u8 *p)
{
	return (u16)p[0] | (u16)p[1] << 8;
}
static inline u32 nrd32 (const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}
static inline void nwr16 (u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xFF);
	p[1] = (u8)((v >> 8) & 0xFF);
}
static inline void nwr32 (u8 *p, u32 v)
{
	p[0] = (u8)(v & 0xFF);
	p[1] = (u8)((v >> 8) & 0xFF);
	p[2] = (u8)((v >> 16) & 0xFF);
	p[3] = (u8)((v >> 24) & 0xFF);
}

//-----------------------------------------------------------------------------


enumError ScanNitroNCGR (nitro_ncgr_t *ncgr, const u8 *data, uint size)
{
	if (!ncgr || !data || size < 0x30 || memcmp (data, "RGCN", 4)
		|| memcmp (data + 0x10, "RAHC", 4))
		return EINVAL;

	const u8 *rahc = data + 0x10;
	const uint num_y = nrd16 (rahc + 0x08);
	const uint num_x = nrd16 (rahc + 0x0a);
	const uint depth = nrd32 (rahc + 0x0c);
	const uint mapping = nrd32 (rahc + 0x10);
	const uint data_size = nrd32 (rahc + 0x18);
	const uint data_off = 8 + nrd32 (rahc + 0x1c);
	const uint bpp = depth == 3 ? 4 : depth == 4 ? 8 : 0;
	if (!bpp || !data_size)
		return EINVAL;
	// data_off is relative to the RAHC chunk, which itself starts at +0x10.
	if ((u64)0x10 + data_off + data_size > size)
		return EINVAL;

	const uint bytes_per_tile = bpp == 4 ? 32 : 64;
	memset (ncgr, 0, sizeof (*ncgr));
	ncgr->tiles = rahc + data_off;
	ncgr->tiles_size = data_size;
	ncgr->bpp = bpp;
	ncgr->n_tiles = data_size / bytes_per_tile;
	ncgr->is_1d = (num_x == 0xFFFF || (mapping & 1) != 0);
	ncgr->mapping_shift = (mapping >> 20) & 7;
	ncgr->tiles_x = (num_x > 0 && num_x != 0xFFFF) ? num_x : 32;
	return ncgr->n_tiles ? ERR_OK : EINVAL;
}

enumError ScanNitroNCLR (nitro_nclr_t *nclr, const u8 *data, uint size)
{
	if (!nclr)
		return EINVAL;
	memset (nclr, 0, sizeof (*nclr));
	if (!data || size < 0x28 || memcmp (data, "RLCN", 4)
		|| memcmp (data + 0x10, "TTLP", 4))
		return EINVAL;

	const uint data_size = nrd32 (data + 0x20);
	const uint data_off = 0x18 + nrd32 (data + 0x24);
	if (!data_size || (data_size & 1) || data_off > size || data_size > size - data_off)
		return EINVAL;

	const uint n = data_size / 2;
	u8 *rgba = MALLOC ((size_t)n * 4);
	if (!rgba)
		return ERR_CANT_CREATE;
	for (uint i = 0; i < n; i++)
	{
		const u16 c = nrd16 (data + data_off + 2 * i);
		rgba[i * 4 + 0] = (u8)((c & 31) * 255 / 31);
		rgba[i * 4 + 1] = (u8)(((c >> 5) & 31) * 255 / 31);
		rgba[i * 4 + 2] = (u8)(((c >> 10) & 31) * 255 / 31);
		rgba[i * 4 + 3] = 255;
	}
	memset (nclr, 0, sizeof (*nclr));
	nclr->rgba = rgba;
	nclr->n_entries = n;
	return ERR_OK;
}

void ResetNitroNCLR (nitro_nclr_t *nclr)
{
	if (!nclr)
		return;
	FREE (nclr->rgba);
	memset (nclr, 0, sizeof (*nclr));
}

//-----------------------------------------------------------------------------
///////////////			OAM decoding			///////////////
//-----------------------------------------------------------------------------

// Hardware OBJ dimensions, indexed [shape][size]. Shape 3 is reserved.
static const u8 oam_dim[3][4][2] = {
	{ { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } }, // square
	{ { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } }, // horizontal
	{ { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } }, // vertical
};

typedef struct oam_t
{
	int x, y; // signed screen position
	uint w, h;
	uint tile; // starting tile index
	uint palette; // palette bank (4bpp only)
	bool color256; // 8bpp
	bool hflip, vflip;
	bool disabled;
} oam_t;

static void decode_oam (oam_t *o, const u8 *rec)
{
	const u16 a0 = nrd16 (rec), a1 = nrd16 (rec + 2), a2 = nrd16 (rec + 4);

	// Attr0: Y is an unsigned 8-bit screen row that wraps; values above the
	// 192-line screen are really negative offsets.
	int y = a0 & 0xFF;
	if (y >= 128)
		y -= 256;

	const bool rot = (a0 >> 8) & 1;
	// Without rotation, bit 9 disables the object; with rotation it doubles
	// the drawn size instead (and there is no disable bit).
	o->disabled = !rot && ((a0 >> 9) & 1);
	o->color256 = (a0 >> 13) & 1;
	uint shape = (a0 >> 14) & 3;
	if (shape > 2)
		shape = 0;

	// Attr1: X is a signed 9-bit value.
	int x = a1 & 0x1FF;
	if (x >= 256)
		x -= 512;
	const uint size = (a1 >> 14) & 3;
	// Flip bits only exist when rotation/scaling is off.
	o->hflip = !rot && ((a1 >> 12) & 1);
	o->vflip = !rot && ((a1 >> 13) & 1);

	o->x = x;
	o->y = y;
	o->w = oam_dim[shape][size][0];
	o->h = oam_dim[shape][size][1];

	// Attr2
	o->tile = a2 & 0x3FF;
	o->palette = (a2 >> 12) & 0xF;
}

// Fetches one pixel's palette index out of the tile array
static int fetch_index (
	const nitro_ncgr_t *ncgr, const oam_t *o, uint px, uint py, bool is_1d, uint mapping_shift)
{
	const uint tiles_across = o->w / 8;
	const uint tx = px / 8, ty = py / 8;
	const uint in_x = px & 7, in_y = py & 7;

	uint tile_index;
	if (is_1d)
	{
		uint base_tile = o->tile << mapping_shift;
		if (o->color256)
			base_tile /= 2;
		tile_index = base_tile + ty * tiles_across + tx;
	}
	else
	{
		// 2D mapping mode: character tiles form a 2D matrix
		const uint sheet_w = ncgr->tiles_x ? ncgr->tiles_x : 32;
		uint base_tile = o->tile;
		if (o->color256)
			base_tile /= 2;
		const uint base_tx = base_tile % sheet_w;
		const uint base_ty = base_tile / sheet_w;
		tile_index = (base_ty + ty) * sheet_w + (base_tx + tx);
	}

	if (ncgr->bpp == 4)
	{
		const uint off = tile_index * 32 + in_y * 4 + in_x / 2;
		if (off >= ncgr->tiles_size)
			return -1;
		const u8 b = ncgr->tiles[off];
		return (in_x & 1) ? (b >> 4) : (b & 0xF);
	}
	const uint off = tile_index * 64 + in_y * 8 + in_x;
	if (off >= ncgr->tiles_size)
		return -1;
	return ncgr->tiles[off];
}

//-----------------------------------------------------------------------------

enumError RenderNCERCell (u8 **dest, uint *width, uint *height, int *ox, int *oy,
	const nintendo_ncer_t *ncer, uint cell_index, const nitro_ncgr_t *ncgr,
	const nitro_nclr_t *nclr)
{
	if (!dest || !width || !height || !ncer || !ncgr || !nclr)
		return EINVAL;

	uint n_obj = 0;
	const u8 *recs = 0;
	enumError err = GetNCERCell (ncer, cell_index, &n_obj, &recs);
	if (err)
		return err;
	if (!n_obj)
		return EINVAL;

	bool is_1d = ncgr->is_1d;
	uint mapping_shift = ncgr->mapping_shift;
	if (ncer)
	{
		if (ncer->mapping_mode <= 3)
		{
			is_1d = true;
			mapping_shift = ncer->mapping_mode;
		}
		else if (ncer->mapping_mode == 4)
		{
			is_1d = false;
			mapping_shift = 0;
		}
	}

	// Pass 1: decode every object and find the bounding box.
	oam_t *objs = CALLOC (n_obj, sizeof (*objs));
	if (!objs)
		return ERR_CANT_CREATE;
	int minx = INT_MAX, miny = INT_MAX, maxx = INT_MIN, maxy = INT_MIN;
	uint n_live = 0;
	for (uint i = 0; i < n_obj; i++)
	{
		decode_oam (objs + i, recs + i * 6);
		if (objs[i].disabled)
			continue;
		n_live++;
		if (objs[i].x < minx)
			minx = objs[i].x;
		if (objs[i].y < miny)
			miny = objs[i].y;
		if ((int)(objs[i].x + objs[i].w) > maxx)
			maxx = objs[i].x + objs[i].w;
		if ((int)(objs[i].y + objs[i].h) > maxy)
			maxy = objs[i].y + objs[i].h;
	}
	if (!n_live)
	{
		FREE (objs);
		return EINVAL;
	}

	const uint w = (uint)(maxx - minx), h = (uint)(maxy - miny);
	if (!w || !h || (u64)w * h > (512u << 20) / 4)
	{
		FREE (objs);
		return EFBIG;
	}

	u8 *rgba = CALLOC (1, (size_t)w * h * 4);
	if (!rgba)
	{
		FREE (objs);
		return ERR_CANT_CREATE;
	}

	// Pass 2: draw back to front.  OAM priority is per-object, but within a
	// cell the reference tools simply paint in list order, later objects on
	// top; index 0 is transparent in both colour modes.
	for (int i = (int)n_obj - 1; i >= 0; i--)
	{
		const oam_t *o = objs + i;
		if (o->disabled)
			continue;
		for (uint py = 0; py < o->h; py++)
			for (uint px = 0; px < o->w; px++)
			{
				const uint sx = o->hflip ? o->w - 1 - px : px;
				const uint sy = o->vflip ? o->h - 1 - py : py;
				const int idx = fetch_index (ncgr, o, sx, sy, is_1d, mapping_shift);
				if (idx <= 0)
					continue; // 0 == transparent, <0 == out of range

				uint pal_index = (uint)idx;
				if (ncgr->bpp == 4)
					pal_index += o->palette * 16;
				if (pal_index >= nclr->n_entries)
					continue;

				const int dx = o->x + (int)px - minx;
				const int dy = o->y + (int)py - miny;
				if (dx < 0 || dy < 0 || dx >= (int)w || dy >= (int)h)
					continue;

				u8 *d = rgba + 4 * ((size_t)dy * w + dx);
				memcpy (d, nclr->rgba + 4 * pal_index, 3);
				d[3] = 255;
			}
	}

	FREE (objs);
	*dest = rgba;
	*width = w;
	*height = h;
	if (ox)
		*ox = minx;
	if (oy)
		*oy = miny;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////			NSCR decoding			///////////////
//-----------------------------------------------------------------------------

enumError ScanNitroNSCR (nitro_nscr_t *nscr, const u8 *data, uint size)
{
	if (!nscr || !data || size < 0x24 || memcmp (data, "RCSN", 4)
		|| memcmp (data + 0x10, "NRCS", 4))
		return EINVAL;

	const u8 *nrcs = data + 0x10;
	const uint data_size = nrd32 (nrcs + 0x10);
	if ((u64)0x14 + 0x10 + data_size > size)
		return EINVAL;

	memset (nscr, 0, sizeof (*nscr));
	nscr->data = nrcs + 0x14;
	nscr->data_size = data_size;
	nscr->width = nrd16 (nrcs + 0x08);
	nscr->height = nrd16 (nrcs + 0x0A);
	nscr->color_mode = nrd16 (nrcs + 0x0C);
	nscr->bg_type = nrd16 (nrcs + 0x0E);
	return ERR_OK;
}

enumError RenderNSCR (u8 **dest, uint *width, uint *height, const nitro_nscr_t *nscr,
	const nitro_ncgr_t *ncgr, const nitro_nclr_t *nclr)
{
	if (!dest || !width || !height || !nscr || !ncgr || !nclr || !nscr->data || !nscr->data_size)
		return EINVAL;

	uint w = nscr->width;
	uint h = nscr->height;
	if (!w || !h)
	{
		const uint total_entries = nscr->data_size / 2;
		if (total_entries == 32 * 24)
		{
			w = 256;
			h = 192;
		}
		else if (total_entries == 32 * 32)
		{
			w = 256;
			h = 256;
		}
		else if (total_entries == 64 * 32)
		{
			w = 512;
			h = 256;
		}
		else
		{
			w = 256;
			h = ((total_entries + 31) / 32) * 8;
		}
	}
	if (!w || !h || w > 1024 || h > 1024)
		return EFBIG;

	u8 *rgba = CALLOC (1, (size_t)w * h * 4);
	if (!rgba)
		return ERR_CANT_CREATE;

	const uint tiles_x = (w + 7) / 8;
	const uint tiles_y = (h + 7) / 8;

	for (uint ty = 0; ty < tiles_y; ty++)
		for (uint tx = 0; tx < tiles_x; tx++)
		{
			const uint idx = ty * tiles_x + tx;
			if (idx * 2 + 2 > nscr->data_size)
				continue;

			const u16 entry = nrd16 (nscr->data + idx * 2);
			const uint tile_id = entry & 0x03FF;
			const bool hflip = (entry >> 10) & 1;
			const bool vflip = (entry >> 11) & 1;
			const uint pal_bank = (entry >> 12) & 0x0F;

			for (uint py = 0; py < 8; py++)
				for (uint px = 0; px < 8; px++)
				{
					const uint sx = hflip ? (7 - px) : px;
					const uint sy = vflip ? (7 - py) : py;

					int col_idx = 0;
					if (ncgr->bpp == 4)
					{
						const uint off = tile_id * 32 + sy * 4 + sx / 2;
						if (off < ncgr->tiles_size)
						{
							const u8 b = ncgr->tiles[off];
							col_idx = (sx & 1) ? (b >> 4) : (b & 0xF);
						}
					}
					else
					{
						const uint off = tile_id * 64 + sy * 8 + sx;
						if (off < ncgr->tiles_size)
							col_idx = ncgr->tiles[off];
					}

					uint pal_index = (uint)col_idx;
					if (ncgr->bpp == 4)
						pal_index += pal_bank * 16;
					if (pal_index >= nclr->n_entries)
						continue;

					const uint dx = tx * 8 + px;
					const uint dy = ty * 8 + py;
					if (dx >= w || dy >= h)
						continue;

					u8 *d = rgba + 4 * ((size_t)dy * w + dx);
					memcpy (d, nclr->rgba + 4 * pal_index, 3);
					d[3] = 255;
				}
		}

	*dest = rgba;
	*width = w;
	*height = h;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		Hudson / IS / Asobimashou 2D Decoders	///////////////
//-----------------------------------------------------------------------------

enumError ScanHudsonNCL (nitro_nclr_t *nclr, const u8 *data, uint size)
{
	if (!nclr || !data || size < 4)
		return EINVAL;
	memset (nclr, 0, sizeof (*nclr));
	uint offset = 0;
	if (size >= 8 && (!memcmp (data, "NCL\0", 4) || !memcmp (data, "NCPR", 4) || !memcmp (data, "5PL0", 4)))
		offset = 8;
	else if (size >= 16 && !memcmp (data, "JNCL", 4))
		offset = 16;
	if (offset >= size)
		return EINVAL;

	const uint data_size = size - offset;
	const uint n = data_size / 2;
	if (!n)
		return EINVAL;
	u8 *rgba = MALLOC ((size_t)n * 4);
	if (!rgba)
		return ERR_CANT_CREATE;
	for (uint i = 0; i < n; i++)
	{
		const u16 c = nrd16 (data + offset + 2 * i);
		rgba[i * 4 + 0] = (u8)((c & 31) * 255 / 31);
		rgba[i * 4 + 1] = (u8)(((c >> 5) & 31) * 255 / 31);
		rgba[i * 4 + 2] = (u8)(((c >> 10) & 31) * 255 / 31);
		rgba[i * 4 + 3] = 255;
	}
	nclr->rgba = rgba;
	nclr->n_entries = n;
	return ERR_OK;
}

enumError ScanHudsonNCG (nitro_ncgr_t *ncgr, const u8 *data, uint size)
{
	if (!ncgr || !data || size < 4)
		return EINVAL;
	memset (ncgr, 0, sizeof (*ncgr));
	uint offset = 0;
	uint bpp = 4;
	if (size >= 8 && (!memcmp (data, "NCG\0", 4) || !memcmp (data, "NCBR", 4) || !memcmp (data, "5CG0", 4)))
	{
		offset = 8;
		if (size >= 12 && data[4] == 8)
			bpp = 8;
	}
	else if (size >= 16 && !memcmp (data, "JNCG", 4))
		offset = 16;

	if (offset >= size)
		return EINVAL;

	const uint data_size = size - offset;
	const uint bytes_per_tile = bpp == 4 ? 32 : 64;
	ncgr->tiles = data + offset;
	ncgr->tiles_size = data_size;
	ncgr->bpp = bpp;
	ncgr->n_tiles = data_size / bytes_per_tile;
	ncgr->is_1d = true;
	ncgr->mapping_shift = 0;
	ncgr->tiles_x = 32;
	return ncgr->n_tiles ? ERR_OK : EINVAL;
}

enumError ScanHudsonNSC (nitro_nscr_t *nscr, const u8 *data, uint size)
{
	if (!nscr || !data || size < 4)
		return EINVAL;
	memset (nscr, 0, sizeof (*nscr));
	uint offset = 0;
	uint w = 256, h = 192;
	if (size >= 8 && (!memcmp (data, "NSC\0", 4) || !memcmp (data, "5SC0", 4)))
	{
		offset = 8;
		if (size >= 12)
		{
			w = nrd16 (data + 4);
			h = nrd16 (data + 6);
		}
	}
	else if (size >= 16 && !memcmp (data, "JNSC", 4))
	{
		offset = 16;
		w = nrd16 (data + 8);
		h = nrd16 (data + 10);
	}
	if (offset >= size)
		return EINVAL;

	nscr->data = data + offset;
	nscr->data_size = size - offset;
	nscr->width = w ? w : 256;
	nscr->height = h ? h : 192;
	nscr->color_mode = 0;
	nscr->bg_type = 0;
	return ERR_OK;
}

enumError ScanISPltt (nitro_nclr_t *nclr, const u8 *data, uint size)
{
	return ScanHudsonNCL (nclr, data, size);
}

enumError ScanISChar (nitro_ncgr_t *ncgr, const u8 *data, uint size)
{
	return ScanHudsonNCG (ncgr, data, size);
}

enumError ScanISScreen (nitro_nscr_t *nscr, const u8 *data, uint size)
{
	return ScanHudsonNSC (nscr, data, size);
}

//-----------------------------------------------------------------------------
///////////////		Nitro 3D Texture Archives (TEX0)		///////////////
//-----------------------------------------------------------------------------

void InitializeNitroTEX0 (nitro_tex0_t *tex0)
{
	if (tex0)
		memset (tex0, 0, sizeof (*tex0));
}

void ResetNitroTEX0 (nitro_tex0_t *tex0)
{
	if (!tex0)
		return;
	if (tex0->textures)
	{
		FREE (tex0->textures);
		tex0->textures = 0;
	}
	if (tex0->palettes)
	{
		for (uint i = 0; i < tex0->n_palettes; i++)
			FREE (tex0->palettes[i].rgba);
		FREE (tex0->palettes);
		tex0->palettes = 0;
	}
	memset (tex0, 0, sizeof (*tex0));
}

// Patricia tree / Dictionary parser for NNS G3D dictionaries
typedef struct dict_entry_t
{
	u16 size_unit;
	u16 ofs_name;
	const u8 *data_ptr;
	const char *names_ptr;
	uint n_entries;
} dict_entry_t;

static bool parse_g3d_dict (dict_entry_t *dict, const u8 *base, uint avail, uint item_size)
{
	if (!dict || !base || avail < 8)
		return false;
	const uint n_entries = base[1];
	if (!n_entries)
		return false;
	const uint ofs_entry = nrd16 (base + 6);
	if (ofs_entry + 4 > avail)
		return false;

	const u8 *pos = base + ofs_entry;
	dict->size_unit = nrd16 (pos + 0);
	dict->ofs_name = nrd16 (pos + 2);
	pos += 4;

	if (pos + item_size * n_entries > base + avail)
		return false;
	dict->data_ptr = pos;
	dict->names_ptr = (const char *)(base + ofs_entry + dict->ofs_name);
	dict->n_entries = n_entries;
	return true;
}

enumError ScanNitroTEX0 (nitro_tex0_t *tex0, const u8 *data, uint size)
{
	if (!tex0 || !data || size < 0x20)
		return EINVAL;
	InitializeNitroTEX0 (tex0);

	const u8 *tex0_hdr = 0;
	uint tex0_avail = 0;

	if (!memcmp (data, "BTX0", 4) || !memcmp (data, "BMD0", 4))
	{
		// Search for TEX0 section
		const uint total_sz = nrd32 (data + 8);
		const uint n_sec = nrd16 (data + 14);
		uint pos = 0x10;
		for (uint s = 0; s < n_sec && pos + 8 <= size; s++)
		{
			const uint sec_sz = nrd32 (data + pos + 4);
			if (!memcmp (data + pos, "TEX0", 4))
			{
				tex0_hdr = data + pos;
				tex0_avail = sec_sz && pos + sec_sz <= size ? sec_sz : size - pos;
				break;
			}
			pos += sec_sz ? sec_sz : 8;
		}
	}
	else if (!memcmp (data, "TEX0", 4))
	{
		tex0_hdr = data;
		tex0_avail = size;
	}

	if (!tex0_hdr || tex0_avail < 0x30)
		return EINVAL;

	tex0->raw_data = data;
	tex0->raw_size = size;

	const uint tex_info_ofs = nrd32 (tex0_hdr + 0x14);
	const uint tex_data_ofs = nrd32 (tex0_hdr + 0x18);
	const uint comp_info_ofs = nrd32 (tex0_hdr + 0x1c);
	const uint comp_data_ofs = nrd32 (tex0_hdr + 0x20);
	const uint comp_pltt_idx_ofs = nrd32 (tex0_hdr + 0x24);
	const uint pltt_info_ofs = nrd32 (tex0_hdr + 0x28);
	const uint pltt_data_ofs = nrd32 (tex0_hdr + 0x2c);

	// 1. Parse Texture Dictionary
	dict_entry_t tex_dict;
	if (tex_info_ofs && tex_info_ofs < tex0_avail
		&& parse_g3d_dict (&tex_dict, tex0_hdr + tex_info_ofs, tex0_avail - tex_info_ofs, 8))
	{
		tex0->n_textures = tex_dict.n_entries;
		tex0->textures = CALLOC (tex_dict.n_entries ? tex_dict.n_entries : 1, sizeof (*tex0->textures));
		for (uint i = 0; i < tex_dict.n_entries; i++)
		{
			nitro_tex_entry_t *t = tex0->textures + i;
			const u8 *entry = tex_dict.data_ptr + i * 8;
			const char *name = tex_dict.names_ptr + i * 16;
			memcpy (t->name, name, 16);
			t->name[16] = 0;

			const u16 offset = nrd16 (entry + 0);
			const u16 param = nrd16 (entry + 2);

			const uint fmt_id = (param >> 10) & 7;
			t->format = (nitro_texfmt_t)fmt_id;
			const uint w_shift = (param >> 4) & 7;
			const uint h_shift = (param >> 7) & 7;
			t->width = 8u << w_shift;
			t->height = 8u << h_shift;
			t->col0_trans = (param >> 13) & 1;

			if (t->format == NITRO_TEXFMT_TEX4x4)
			{
				if (comp_data_ofs + (uint)offset * 8 <= tex0_avail)
				{
					t->texels = tex0_hdr + comp_data_ofs + (uint)offset * 8;
					t->texels_size = (t->width * t->height) / 2;
				}
				if (comp_pltt_idx_ofs + (uint)offset * 4 <= tex0_avail)
				{
					t->comp_idx = tex0_hdr + comp_pltt_idx_ofs + (uint)offset * 4;
					t->comp_idx_size = (t->width * t->height) / 8;
				}
			}
			else
			{
				if (tex_data_ofs + (uint)offset * 8 <= tex0_avail)
				{
					t->texels = tex0_hdr + tex_data_ofs + (uint)offset * 8;
					static const u8 bpps[] = { 0, 8, 2, 4, 8, 2, 8, 16 };
					t->texels_size = (t->width * t->height * bpps[fmt_id <= 7 ? fmt_id : 0]) / 8;
				}
			}
		}
	}

	// 2. Parse Palette Dictionary
	dict_entry_t pltt_dict;
	if (pltt_info_ofs && pltt_info_ofs < tex0_avail
		&& parse_g3d_dict (&pltt_dict, tex0_hdr + pltt_info_ofs, tex0_avail - pltt_info_ofs, 4))
	{
		tex0->n_palettes = pltt_dict.n_entries;
		tex0->palettes = CALLOC (pltt_dict.n_entries ? pltt_dict.n_entries : 1, sizeof (*tex0->palettes));
		for (uint i = 0; i < pltt_dict.n_entries; i++)
		{
			nitro_pltt_entry_t *p = tex0->palettes + i;
			const u8 *entry = pltt_dict.data_ptr + i * 4;
			const char *name = pltt_dict.names_ptr + i * 16;
			memcpy (p->name, name, 16);
			p->name[16] = 0;

			const u16 offset = nrd16 (entry + 0);
			const u16 flag = nrd16 (entry + 2);

			if (pltt_data_ofs + (uint)offset * 8 <= tex0_avail)
			{
				p->raw_data = tex0_hdr + pltt_data_ofs + (uint)offset * 8;
				// Calculate palette color count (up to next palette or end of section)
				uint avail_bytes = tex0_avail - (pltt_data_ofs + (uint)offset * 8);
				p->n_colors = (flag & 1) ? 4 : (avail_bytes / 2);
				if (p->n_colors > 256)
					p->n_colors = 256;
				if (p->n_colors > 0)
				{
					p->rgba = CALLOC (p->n_colors, 4);
					for (uint c = 0; c < p->n_colors; c++)
					{
						const u16 bgr = nrd16 (p->raw_data + c * 2);
						p->rgba[c * 4 + 0] = (u8)((bgr & 31) * 255 / 31);
						p->rgba[c * 4 + 1] = (u8)(((bgr >> 5) & 31) * 255 / 31);
						p->rgba[c * 4 + 2] = (u8)(((bgr >> 10) & 31) * 255 / 31);
						p->rgba[c * 4 + 3] = 255;
					}
				}
			}
		}
	}

	return (tex0->n_textures > 0 || tex0->n_palettes > 0) ? ERR_OK : EINVAL;
}

static inline u32 sample_palette_color (const nitro_pltt_entry_t *pltt, uint idx)
{
	if (!pltt || !pltt->rgba || idx >= pltt->n_colors)
		return 0xFF000000; // default black opaque
	const u8 *c = pltt->rgba + idx * 4;
	return (u32)c[0] | ((u32)c[1] << 8) | ((u32)c[2] << 16) | ((u32)c[3] << 24);
}

static inline u32 blend_colors (u32 c1, u32 c2, int factor)
{
	const uint r1 = c1 & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = (c1 >> 16) & 0xFF;
	const uint r2 = c2 & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = (c2 >> 16) & 0xFF;
	const uint r = (r1 * (8 - factor) + r2 * factor + 4) / 8;
	const uint g = (g1 * (8 - factor) + g2 * factor + 4) / 8;
	const uint b = (b1 * (8 - factor) + b2 * factor + 4) / 8;
	return (r & 0xFF) | ((g & 0xFF) << 8) | ((b & 0xFF) << 16) | 0xFF000000;
}

enumError DecodeNitroTexture_RGBA (u8 **dest, uint *width, uint *height,
	const nitro_tex0_t *tex0, uint tex_idx, int pltt_idx)
{
	if (!dest || !width || !height || !tex0 || tex_idx >= tex0->n_textures)
		return EINVAL;

	const nitro_tex_entry_t *tex = tex0->textures + tex_idx;
	if (!tex->width || !tex->height || !tex->texels)
		return EINVAL;

	// Palette matching
	const nitro_pltt_entry_t *pltt = 0;
	if (pltt_idx >= 0 && (uint)pltt_idx < tex0->n_palettes)
		pltt = tex0->palettes + pltt_idx;
	else
	{
		// Match by name
		for (uint p = 0; p < tex0->n_palettes; p++)
			if (!strcasecmp (tex0->palettes[p].name, tex->name))
			{
				pltt = tex0->palettes + p;
				break;
			}
		if (!pltt && tex0->n_palettes > 0)
			pltt = tex0->palettes + 0;
	}

	const uint w = tex->width, h = tex->height;
	u8 *rgba = CALLOC (1, (size_t)w * h * 4);
	if (!rgba)
		return ERR_CANT_CREATE;

	u32 *out32 = (u32 *)rgba;

	switch (tex->format)
	{
		case NITRO_TEXFMT_A3I5:
		{
			for (uint y = 0; y < h; y++)
				for (uint x = 0; x < w; x++)
				{
					const u8 d = tex->texels[y * w + x];
					const uint idx = d & 0x1F;
					const uint a3 = (d >> 5) & 7;
					const uint a8 = (a3 * 255 + 3) / 7;
					u32 c = sample_palette_color (pltt, idx);
					out32[y * w + x] = (c & 0x00FFFFFF) | (a8 << 24);
				}
			break;
		}
		case NITRO_TEXFMT_PLTT4:
		{
			for (uint y = 0; y < h; y++)
				for (uint x = 0; x < w; x++)
				{
					const uint byte_idx = (y * w + x) >> 2;
					const uint shift = ((y * w + x) & 3) * 2;
					const uint idx = (tex->texels[byte_idx] >> shift) & 3;
					if (idx == 0 && tex->col0_trans)
						out32[y * w + x] = 0;
					else
						out32[y * w + x] = sample_palette_color (pltt, idx);
				}
			break;
		}
		case NITRO_TEXFMT_PLTT16:
		{
			for (uint y = 0; y < h; y++)
				for (uint x = 0; x < w; x++)
				{
					const uint byte_idx = (y * w + x) >> 1;
					const uint shift = ((y * w + x) & 1) * 4;
					const uint idx = (tex->texels[byte_idx] >> shift) & 0xF;
					if (idx == 0 && tex->col0_trans)
						out32[y * w + x] = 0;
					else
						out32[y * w + x] = sample_palette_color (pltt, idx);
				}
			break;
		}
		case NITRO_TEXFMT_PLTT256:
		{
			for (uint y = 0; y < h; y++)
				for (uint x = 0; x < w; x++)
				{
					const uint idx = tex->texels[y * w + x];
					if (idx == 0 && tex->col0_trans)
						out32[y * w + x] = 0;
					else
						out32[y * w + x] = sample_palette_color (pltt, idx);
				}
			break;
		}
		case NITRO_TEXFMT_TEX4x4:
		{
			if (!tex->comp_idx)
				break;
			const uint blocks_x = (w + 3) / 4;
			const uint blocks_y = (h + 3) / 4;
			for (uint by = 0; by < blocks_y; by++)
				for (uint bx = 0; bx < blocks_x; bx++)
				{
					const uint bidx = by * blocks_x + bx;
					const u32 texel32 = nrd32 (tex->texels + bidx * 4);
					const u16 pidx16 = nrd16 (tex->comp_idx + bidx * 2);
					const uint pal_base = (pidx16 & 0x3FFF) * 2;
					const bool interpolate = (pidx16 & 0x8000) != 0;
					const bool opaque = (pidx16 & 0x4000) != 0;

					u32 c[4] = { 0 };
					c[0] = sample_palette_color (pltt, pal_base + 0);
					c[1] = sample_palette_color (pltt, pal_base + 1);

					if (!interpolate)
					{
						c[2] = sample_palette_color (pltt, pal_base + 2);
						c[3] = opaque ? sample_palette_color (pltt, pal_base + 3) : 0;
					}
					else
					{
						if (opaque)
						{
							c[2] = blend_colors (c[0], c[1], 3);
							c[3] = blend_colors (c[0], c[1], 5);
						}
						else
						{
							c[2] = blend_colors (c[0], c[1], 4);
							c[3] = 0;
						}
					}

					for (uint py = 0; py < 4; py++)
						for (uint px = 0; px < 4; px++)
						{
							const uint gx = bx * 4 + px;
							const uint gy = by * 4 + py;
							if (gx < w && gy < h)
							{
								const uint slot = px + py * 4;
								const uint val = (texel32 >> (slot * 2)) & 3;
								out32[gy * w + gx] = c[val];
							}
						}
				}
			break;
		}
		case NITRO_TEXFMT_A5I3:
		{
			for (uint y = 0; y < h; y++)
				for (uint x = 0; x < w; x++)
				{
					const u8 d = tex->texels[y * w + x];
					const uint idx = d & 7;
					const uint a5 = (d >> 3) & 0x1F;
					const uint a8 = (a5 * 255 + 15) / 31;
					u32 c = sample_palette_color (pltt, idx);
					out32[y * w + x] = (c & 0x00FFFFFF) | (a8 << 24);
				}
			break;
		}
		case NITRO_TEXFMT_DIRECT:
		{
			for (uint y = 0; y < h; y++)
				for (uint x = 0; x < w; x++)
				{
					const u16 c16 = nrd16 (tex->texels + (y * w + x) * 2);
					const u8 r = (u8)((c16 & 31) * 255 / 31);
					const u8 g = (u8)(((c16 >> 5) & 31) * 255 / 31);
					const u8 b = (u8)(((c16 >> 10) & 31) * 255 / 31);
					const u8 a = (c16 & 0x8000) ? 255 : 0;
					rgba[(y * w + x) * 4 + 0] = r;
					rgba[(y * w + x) * 4 + 1] = g;
					rgba[(y * w + x) * 4 + 2] = b;
					rgba[(y * w + x) * 4 + 3] = a;
				}
			break;
		}
		default:
			FREE (rgba);
			return EINVAL;
	}

	*dest = rgba;
	*width = w;
	*height = h;
	return ERR_OK;
}

enumError DecodeNSBTX_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, uint size)
{
	nitro_tex0_t tex0;
	enumError err = ScanNitroTEX0 (&tex0, data, size);
	if (err)
		return err;
	if (tex0.n_textures == 0)
	{
		ResetNitroTEX0 (&tex0);
		return EINVAL;
	}
	err = DecodeNitroTexture_RGBA (dest, width, height, &tex0, 0, -1);
	ResetNitroTEX0 (&tex0);
	return err;
}

enumError CreateNSBTX (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height,
	nitro_texfmt_t fmt, ccp tex_name, ccp pltt_name)
{
	if (!dest || !dest_size || !rgba || !width || !height)
		return EINVAL;

	// Build direct color or simple indexed NSBTX file
	const uint tex_data_size = width * height * 2; // Direct format 16-bit
	const uint total_file_size = 0x10 + 0x3c + 0x24 + tex_data_size;

	u8 *out = CALLOC (1, total_file_size);
	if (!out)
		return ERR_CANT_CREATE;

	// BTX0 Header
	memcpy (out + 0x00, "BTX0", 4);
	out[0x04] = 0xFF; out[0x05] = 0xFE; // BOM LE
	out[0x06] = 0x00; out[0x07] = 0x01; // Version 1.0
	nwr32 (out + 0x08, total_file_size);
	nwr16 (out + 0x0c, 0x10); // Header size
	nwr16 (out + 0x0e, 1);    // 1 section

	// TEX0 Section
	u8 *tex0 = out + 0x10;
	memcpy (tex0 + 0x00, "TEX0", 4);
	nwr32 (tex0 + 0x04, total_file_size - 0x10);
	nwr32 (tex0 + 0x14, 0x3c); // texInfoOfs
	nwr32 (tex0 + 0x18, 0x3c + 0x24); // texDataOfs

	// Dict
	u8 *dict = tex0 + 0x3c;
	dict[0] = 0;
	dict[1] = 1; // 1 entry
	nwr16 (dict + 2, 0x24);
	nwr16 (dict + 4, 8); // entry size
	nwr16 (dict + 6, 8); // ofs entry
	nwr16 (dict + 8, 8); // size unit
	nwr16 (dict + 10, 12); // ofs name from pos (pos + 12 = dict + 20)
	nwr16 (dict + 12, 0); // texel offset 0
	// 16-bit param: fmt=7 (direct)
	uint w_shift = 0, h_shift = 0;
	while ((8u << w_shift) < width && w_shift < 7) w_shift++;
	while ((8u << h_shift) < height && h_shift < 7) h_shift++;
	const u16 param = (7 << 10) | (h_shift << 7) | (w_shift << 4) | 0x2000;
	nwr16 (dict + 14, param);
	char name_buf[16] = { 0 };
	snprintf (name_buf, sizeof (name_buf), "%s", tex_name ? tex_name : "tex0");
	memcpy (dict + 20, name_buf, 16);

	// Convert pixels to BGR555 direct
	u8 *dst_texels = tex0 + 0x3c + 0x24;
	for (uint y = 0; y < height; y++)
		for (uint x = 0; x < width; x++)
		{
			const u8 *p = rgba + (y * width + x) * 4;
			const u16 r = (p[0] * 31 + 127) / 255;
			const u16 g = (p[1] * 31 + 127) / 255;
			const u16 b = (p[2] * 31 + 127) / 255;
			const u16 a = p[3] >= 128 ? 0x8000 : 0;
			const u16 c16 = r | (g << 5) | (b << 10) | a;
			nwr16 (dst_texels + (y * width + x) * 2, c16);
		}

	*dest = out;
	*dest_size = total_file_size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		Single Texture Decoders (5TX / SPT / NTGA)	///////////////
//-----------------------------------------------------------------------------

enumError Decode5TX_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size)
{
	if (!dest || !width || !height || !src || src_size < 16)
		return EINVAL;
	const uint w = nrd16 (src + 4);
	const uint h = nrd16 (src + 6);
	const uint fmt = src[8];
	if (!w || !h || (u64)w * h > (256u << 20) / 4)
		return EINVAL;
	u8 *rgba = CALLOC (1, (size_t)w * h * 4);
	if (!rgba)
		return ERR_CANT_CREATE;
	if (fmt == 7 || fmt == 0) // Direct 16-bit
	{
		const u8 *texels = src + 16;
		for (uint i = 0; i < w * h && 16 + i * 2 + 2 <= src_size; i++)
		{
			const u16 c16 = nrd16 (texels + i * 2);
			rgba[i * 4 + 0] = (u8)((c16 & 31) * 255 / 31);
			rgba[i * 4 + 1] = (u8)(((c16 >> 5) & 31) * 255 / 31);
			rgba[i * 4 + 2] = (u8)(((c16 >> 10) & 31) * 255 / 31);
			rgba[i * 4 + 3] = (c16 & 0x8000) ? 255 : 0;
		}
	}
	*dest = rgba;
	*width = w;
	*height = h;
	return ERR_OK;
}

enumError Encode5TX_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height)
{
	if (!dest || !dest_size || !rgba || !width || !height)
		return EINVAL;
	const uint total = 16 + width * height * 2;
	u8 *out = CALLOC (1, total);
	if (!out)
		return ERR_CANT_CREATE;
	memcpy (out, "5TX0", 4);
	nwr16 (out + 4, width);
	nwr16 (out + 6, height);
	out[8] = 7; // direct format
	for (uint i = 0; i < width * height; i++)
	{
		const u8 *p = rgba + i * 4;
		const u16 c = ((p[0] * 31 / 255) & 31) | (((p[1] * 31 / 255) & 31) << 5)
			| (((p[2] * 31 / 255) & 31) << 10) | (p[3] >= 128 ? 0x8000 : 0);
		nwr16 (out + 16 + i * 2, c);
	}
	*dest = out;
	*dest_size = total;
	return ERR_OK;
}

enumError DecodeSPT_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size)
{
	return Decode5TX_RGBA (dest, width, height, src, src_size);
}

enumError EncodeSPT_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height)
{
	return Encode5TX_RGBA (dest, dest_size, rgba, width, height);
}

enumError DecodeNTGA_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size)
{
	return Decode5TX_RGBA (dest, width, height, src, src_size);
}

//-----------------------------------------------------------------------------
///////////////		Nitro Font Formats (NFTR / BNFR)		///////////////
//-----------------------------------------------------------------------------

void InitializeNitroNFTR (nitro_nftr_t *nftr)
{
	if (nftr)
		memset (nftr, 0, sizeof (*nftr));
}

void ResetNitroNFTR (nitro_nftr_t *nftr)
{
	if (!nftr)
		return;
	FREE (nftr->glyphs);
	memset (nftr, 0, sizeof (*nftr));
}

enumError ScanNitroNFTR (nitro_nftr_t *nftr, const u8 *data, uint size)
{
	if (!nftr || !data || size < 0x20)
		return EINVAL;
	InitializeNitroNFTR (nftr);

	const bool is_bnfr = !memcmp (data, "RNFB", 4) || !memcmp (data, "BNFR", 4);
	const bool is_nftr = !memcmp (data, "RTNF", 4) || !memcmp (data, "FNTR", 4);
	if (!is_bnfr && !is_nftr)
		return EINVAL;

	nftr->is_bnfr = is_bnfr;
	nftr->version = nrd16 (data + 6);
	const uint n_sections = nrd16 (data + 14);

	uint pos = 0x10;
	const u8 *finf = 0, *cglp = 0, *cmap = 0, *cwrd = 0;

	for (uint s = 0; s < n_sections && pos + 8 <= size; s++)
	{
		const uint sec_sz = nrd32 (data + pos + 4);
		if (!memcmp (data + pos, "FINF", 4) || !memcmp (data + pos, "FNIF", 4))
			finf = data + pos;
		else if (!memcmp (data + pos, "CGLP", 4) || !memcmp (data + pos, "PLGC", 4))
			cglp = data + pos;
		else if (!memcmp (data + pos, "CMAP", 4) || !memcmp (data + pos, "PAMC", 4))
			cmap = data + pos;
		else if (!memcmp (data + pos, "CWRD", 4) || !memcmp (data + pos, "DRWC", 4)
			|| !memcmp (data + pos, "TGLP", 4))
			cwrd = data + pos;
		pos += sec_sz ? sec_sz : 8;
	}

	if (finf && pos <= size)
	{
		nftr->linefeed = finf[0x09];
		nftr->cell_w = finf[0x0e];
		nftr->cell_h = finf[0x0f];
	}

	if (cglp)
	{
		nftr->cell_w = cglp[0x08];
		nftr->cell_h = cglp[0x09];
		nftr->bpp = cglp[0x0c];
		if (!nftr->bpp) nftr->bpp = 1;
		nftr->max_advance = cglp[0x0b];
		const uint data_sz = nrd32 (cglp + 4) - 0x10;
		const uint bytes_per_glyph = (nftr->cell_w * nftr->cell_h * nftr->bpp + 7) / 8;
		nftr->glyph_data = cglp + 0x10;
		nftr->glyph_data_size = data_sz;
		nftr->n_glyphs = bytes_per_glyph ? (data_sz / bytes_per_glyph) : 0;
	}

	if (!nftr->cell_w) nftr->cell_w = 8;
	if (!nftr->cell_h) nftr->cell_h = 8;
	if (!nftr->bpp) nftr->bpp = 1;

	// Populate basic glyph mapping
	if (nftr->n_glyphs > 0)
	{
		nftr->n_mapped_glyphs = nftr->n_glyphs;
		nftr->glyphs = CALLOC (nftr->n_glyphs, sizeof (*nftr->glyphs));
		for (uint i = 0; i < nftr->n_glyphs; i++)
		{
			nftr->glyphs[i].char_code = (u16)(32 + i);
			nftr->glyphs[i].glyph_index = (u16)i;
			nftr->glyphs[i].width = (u8)nftr->cell_w;
			nftr->glyphs[i].advance = (u8)nftr->cell_w;
		}
	}

	return nftr->n_glyphs ? ERR_OK : EINVAL;
}

enumError DecodeNFTR_Atlas (u8 **dest_atlas, uint *atlas_w, uint *atlas_h,
	char **dest_xml, const u8 *data, uint size)
{
	if (!dest_atlas || !atlas_w || !atlas_h || !dest_xml || !data || size < 0x20)
		return EINVAL;

	nitro_nftr_t nftr;
	enumError err = ScanNitroNFTR (&nftr, data, size);
	if (err)
		return err;

	const uint cols = 16;
	const uint rows = (nftr.n_glyphs + cols - 1) / cols;
	const uint aw = cols * nftr.cell_w;
	const uint ah = rows * nftr.cell_h;

	u8 *atlas = CALLOC (1, (size_t)aw * ah * 4);
	if (!atlas)
	{
		ResetNitroNFTR (&nftr);
		return ERR_CANT_CREATE;
	}

	const uint bytes_per_glyph = (nftr.cell_w * nftr.cell_h * nftr.bpp + 7) / 8;

	for (uint g = 0; g < nftr.n_glyphs; g++)
	{
		const uint gx = (g % cols) * nftr.cell_w;
		const uint gy = (g / cols) * nftr.cell_h;
		const u8 *gsrc = nftr.glyph_data + g * bytes_per_glyph;

		uint bitpos = 0;
		for (uint py = 0; py < nftr.cell_h; py++)
			for (uint px = 0; px < nftr.cell_w; px++)
			{
				uint val = 0;
				if (nftr.bpp == 1)
				{
					val = (gsrc[bitpos / 8] >> (7 - (bitpos & 7))) & 1;
					val = val ? 255 : 0;
				}
				else if (nftr.bpp == 2)
				{
					val = (gsrc[bitpos / 8] >> (6 - (bitpos & 6))) & 3;
					val = (val * 255) / 3;
				}
				else if (nftr.bpp == 4)
				{
					val = (gsrc[bitpos / 8] >> (4 - (bitpos & 4))) & 0xF;
					val = (val * 255) / 15;
				}
				else
				{
					val = gsrc[bitpos / 8];
				}
				bitpos += nftr.bpp;

				const uint dx = gx + px;
				const uint dy = gy + py;
				u8 *d = atlas + (dy * aw + dx) * 4;
				d[0] = 255; d[1] = 255; d[2] = 255;
				d[3] = (u8)val;
			}
	}

	// Generate sibling XML metrics descriptor
	char xml_buf[4096];
	snprintf (xml_buf, sizeof (xml_buf),
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<font format=\"%s\" version=\"0x%04x\" cell_w=\"%u\" cell_h=\"%u\" bpp=\"%u\" linefeed=\"%u\" n_glyphs=\"%u\">\n"
		"  <sheet width=\"%u\" height=\"%u\" columns=\"%u\" rows=\"%u\"/>\n"
		"</font>\n",
		nftr.is_bnfr ? "BNFR" : "NFTR", nftr.version, nftr.cell_w, nftr.cell_h,
		nftr.bpp, nftr.linefeed, nftr.n_glyphs, aw, ah, cols, rows);

	*dest_atlas = atlas;
	*atlas_w = aw;
	*atlas_h = ah;
	*dest_xml = STRDUP (xml_buf);
	ResetNitroNFTR (&nftr);
	return ERR_OK;
}

enumError EncodeNFTR_Atlas (u8 **dest, uint *dest_size,
	const u8 *atlas_rgba, uint atlas_w, uint atlas_h, ccp xml_str, bool is_bnfr)
{
	if (!dest || !dest_size || !atlas_rgba || !atlas_w || !atlas_h)
		return EINVAL;

	const uint cell_w = 8, cell_h = 8, bpp = 1;
	const uint cols = atlas_w / cell_w;
	const uint rows = atlas_h / cell_h;
	const uint n_glyphs = cols * rows;
	const uint glyph_bytes = (cell_w * cell_h * bpp + 7) / 8;
	const uint cglp_sz = 0x10 + n_glyphs * glyph_bytes;
	const uint total_sz = 0x10 + 0x20 + cglp_sz + 0x20;

	u8 *out = CALLOC (1, total_sz);
	if (!out)
		return ERR_CANT_CREATE;

	// Header
	memcpy (out, is_bnfr ? "RNFB" : "RTNF", 4);
	out[4] = 0xFF; out[5] = 0xFE; // BOM
	nwr16 (out + 6, 0x0102); // Version
	nwr32 (out + 8, total_sz);
	nwr16 (out + 12, 0x10);
	nwr16 (out + 14, 3); // 3 sections (FINF, CGLP, CMAP)

	// FINF
	u8 *finf = out + 0x10;
	memcpy (finf, "FINF", 4);
	nwr32 (finf + 4, 0x20);
	finf[8] = 1; finf[9] = (u8)cell_h; finf[14] = (u8)cell_w; finf[15] = (u8)cell_h;

	// CGLP
	u8 *cglp = out + 0x30;
	memcpy (cglp, "CGLP", 4);
	nwr32 (cglp + 4, cglp_sz);
	cglp[8] = (u8)cell_w; cglp[9] = (u8)cell_h; cglp[11] = (u8)cell_w; cglp[12] = (u8)bpp;

	u8 *gdst = cglp + 0x10;
	for (uint g = 0; g < n_glyphs; g++)
	{
		const uint gx = (g % cols) * cell_w;
		const uint gy = (g / cols) * cell_h;
		u8 *out_glyph = gdst + g * glyph_bytes;
		uint bitpos = 0;
		for (uint py = 0; py < cell_h; py++)
			for (uint px = 0; px < cell_w; px++)
			{
				const u8 *p = atlas_rgba + ((gy + py) * atlas_w + (gx + px)) * 4;
				if (p[3] >= 128)
					out_glyph[bitpos / 8] |= 0x80 >> (bitpos & 7);
				bitpos++;
			}
	}

	*dest = out;
	*dest_size = total_sz;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		Nitro 2D Layout Formats (BNLL)			///////////////
//-----------------------------------------------------------------------------

enumError DecodeBNLL_Text (char **dest_text, const u8 *data, uint size)
{
	if (!dest_text || !data || size < 0x10)
		return EINVAL;

	const bool is_bnll = !memcmp (data, "LLNB", 4) || !memcmp (data, "BNLL", 4);
	const bool is_bncl = !memcmp (data, "LCNB", 4) || !memcmp (data, "BNCL", 4);
	const bool is_bnbl = !memcmp (data, "LBNB", 4) || !memcmp (data, "BNBL", 4);

	if (!is_bnll && !is_bncl && !is_bnbl)
		return EINVAL;

	const uint ver = nrd16 (data + 6);
	const uint n_sections = nrd16 (data + 14);

	char buf[4096];
	snprintf (buf, sizeof (buf),
		"# Nitro DS 2D Layout (%s)\n"
		"version = 0x%04x\n"
		"sections = %u\n"
		"size = %u\n",
		is_bnll ? "BNLL" : is_bncl ? "BNCL" : "BNBL", ver, n_sections, size);

	*dest_text = STRDUP (buf);
	return ERR_OK;
}

enumError EncodeBNLL_Text (u8 **dest, uint *dest_size, ccp text)
{
	if (!dest || !dest_size || !text)
		return EINVAL;

	const uint total = 0x20;
	u8 *out = CALLOC (1, total);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, "LLNB", 4);
	out[4] = 0xFF; out[5] = 0xFE;
	nwr16 (out + 6, 0x0100);
	nwr32 (out + 8, total);
	nwr16 (out + 12, 0x10);
	nwr16 (out + 14, 1);

	*dest = out;
	*dest_size = total;
	return ERR_OK;
}

