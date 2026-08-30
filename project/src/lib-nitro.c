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
