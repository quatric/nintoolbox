// lib-nsmbw.c – NSMBW / Newer Super Mario Bros. Wii tileset format support
// Decodes U8-arc tilesets: RGB4A3 texture, tile behaviour, object definitions.

#include "lib-nsmbw.h"
#include "lib-xmsg.h"    // strbuf helpers (sb_init, sb_putf, etc.) defined below locally
#include "lib-lz10.h"
#include "lib-std.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

//
///////////////////////////////////////////////////////////////////////////////
//////////////////////////  Local string-buffer  //////////////////////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct nsmbw_sb_t
{
	char *buf;
	size_t len;
	size_t cap;
} nsmbw_sb_t;

static void nsb_init (nsmbw_sb_t *s)
{
	s->cap = 8192;
	s->len = 0;
	s->buf = MALLOC (s->cap);
	if (s->buf) s->buf[0] = 0;
}

static void nsb_grow (nsmbw_sb_t *s, size_t extra)
{
	if (!s->buf) return;
	if (s->len + extra + 1 <= s->cap) return;
	size_t nc = s->cap * 2 + extra + 128;
	char *nb = REALLOC (s->buf, nc);
	if (!nb) return;
	s->buf = nb;
	s->cap = nc;
}

static void nsb_putc (nsmbw_sb_t *s, char c)
{
	nsb_grow (s, 1);
	if (s->buf) { s->buf[s->len++] = c; s->buf[s->len] = 0; }
}

static void nsb_puts (nsmbw_sb_t *s, const char *str)
{
	if (!str) return;
	size_t l = strlen (str);
	nsb_grow (s, l);
	if (s->buf) { memcpy (s->buf + s->len, str, l + 1); s->len += l; }
}

static void nsb_printf (nsmbw_sb_t *s, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));

static void nsb_printf (nsmbw_sb_t *s, const char *fmt, ...)
{
	if (!s->buf) return;
	char tmp[512];
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (tmp, sizeof (tmp), fmt, ap);
	va_end (ap);
	nsb_puts (s, tmp);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////////////  Arc path detection  //////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

bool IsNSMBWTilesetArc (const char *const *paths, uint n_paths)
{
	// Must have at least a BG_tex entry and a BG_chk entry.
	bool have_tex = false, have_chk = false;
	for (uint i = 0; i < n_paths; i++)
	{
		const char *p = paths[i];
		if (!p) continue;
		if (strncmp (p, "BG_tex/", 7) == 0 &&
			strstr (p, "_tex.bin"))
			have_tex = true;
		if (strncmp (p, "BG_chk/", 7) == 0 &&
			strstr (p, "d_bgchk_"))
			have_chk = true;
	}
	return have_tex && have_chk;
}

//
///////////////////////////////////////////////////////////////////////////////
////////////////////  RGB4A3 / RGB555 GX Texture decode  //////////////////////
///////////////////////////////////////////////////////////////////////////////
//
//  The GX RGB4A3 texture format uses 4×4 texel blocks.  Each 2-byte word is
//  either:
//    0aaarrrrggggbbbb  (RGB4A3: alpha 0..7 → 0..255, 4-bit colour channels)
//    1rrrrrgggggbbbbb  (RGB555: fully opaque)
//
//  The tileset texture is 1024 × 256 pixels = 256 × 64 texels = 16384 blocks.
//  Block layout: (xtile=0..255, ytile=0..63), each block 4×4 pixels.
//
//  After decompression the raw data is 1024*256*2 = 524288 bytes.
//  Output is 1024*256*4 ARGB8 bytes (LE: BGRA byte order on little-endian
//  host, but stored as A<<24|R<<16|G<<8|B so callers treat it as u32 ARGB).
//
///////////////////////////////////////////////////////////////////////////////

static inline u32 rgb4a3_word_to_argb (u16 w)
{
	if (w & 0x8000)
	{
		// RGB555 – fully opaque
		u32 r = (w >> 10) & 0x1F; r = (r << 3) | (r >> 2);
		u32 g = (w >>  5) & 0x1F; g = (g << 3) | (g >> 2);
		u32 b = (w      ) & 0x1F; b = (b << 3) | (b >> 2);
		return 0xFF000000u | (r << 16) | (g << 8) | b;
	}
	else
	{
		// RGB4A3
		u32 a = (w >> 12) & 0x7; a = (a << 5) | (a << 2) | (a >> 1);
		u32 r = (w >>  8) & 0xF; r *= 17;
		u32 g = (w >>  4) & 0xF; g *= 17;
		u32 b = (w      ) & 0xF; b *= 17;
		return (a << 24) | (r << 16) | (g << 8) | b;
	}
}

void DecodeRGB4A3 (u8 *dst, const u8 *src, uint src_size)
{
	// Output is 1024×256 pixels in ARGB8 (4 bytes each, stored A,R,G,B).
	// GX block layout: blocks of 4×4 pixels, row-major over texture.
	// tex_w = 1024, tex_h = 256, blocks_per_row = 256, total_rows_of_blocks = 64.
	//
	// Block numbering: block[y_blk][x_blk]
	//   pixel_x = x_blk * 4 + (word_idx % 4)
	//   pixel_y = y_blk * 4 + (word_idx / 4)

	const uint W = 1024, H = 256;
	const uint words = W * H; // 262144

	if (!src || src_size < words * 2)
		return;

	for (uint i = 0; i < words; i++)
	{
		uint x_blk = (i / 16) % (W / 4);       // block column
		uint y_blk = (i / 16) / (W / 4);       // block row
		uint in_blk = i % 16;
		uint px = x_blk * 4 + (in_blk % 4);
		uint py = y_blk * 4 + (in_blk / 4);

		u16 w = (u16)src[i * 2] << 8 | src[i * 2 + 1];
		u32 argb = rgb4a3_word_to_argb (w);

		u8 *p = dst + (py * W + px) * 4;
		p[0] = (argb >> 16) & 0xFF; // R
		p[1] = (argb >>  8) & 0xFF; // G
		p[2] = (argb      ) & 0xFF; // B
		p[3] = (argb >> 24) & 0xFF; // A
	}
}

//
///////////////////////////////////////////////////////////////////////////////
////////////////////  RGB4A3 / RGB555 GX Texture encode  //////////////////////
///////////////////////////////////////////////////////////////////////////////
//
// Encode flat RGBA8 (4 bytes: R,G,B,A) back to GX block-4 RGB4A3/RGB555.
// src is 1024×256×4 bytes (R,G,B,A per pixel).
// dst must be 524288 bytes.
//
///////////////////////////////////////////////////////////////////////////////

static inline u16 argb_to_rgb4a3_word (u8 r, u8 g, u8 b, u8 a)
{
	if (a >= 238)
	{
		// RGB555 – fully opaque
		u16 rv = ((r + 4) << 2) / 33;
		u16 gv = ((g + 4) << 2) / 33;
		u16 bv = ((b + 4) << 2) / 33;
		return (u16)(0x8000 | (rv << 10) | (gv << 5) | bv);
	}
	else
	{
		// RGB4A3
		u16 av = (u16)(((a + 18) << 1) / 73);
		u16 rv = (r + 8) / 17;
		u16 gv = (g + 8) / 17;
		u16 bv = (b + 8) / 17;
		return (u16)((av << 12) | (rv << 8) | (gv << 4) | bv);
	}
}

void EncodeRGB4A3 (u8 *dst, const u8 *src)
{
	const uint W = 1024, H = 256;
	const uint words = W * H;

	for (uint i = 0; i < words; i++)
	{
		uint x_blk = (i / 16) % (W / 4);
		uint y_blk = (i / 16) / (W / 4);
		uint in_blk = i % 16;
		uint px = x_blk * 4 + (in_blk % 4);
		uint py = y_blk * 4 + (in_blk / 4);

		const u8 *p = src + (py * W + px) * 4;
		u16 w = argb_to_rgb4a3_word (p[0], p[1], p[2], p[3]);
		dst[i * 2]     = (u8)(w >> 8);
		dst[i * 2 + 1] = (u8)(w & 0xFF);
	}
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////  Object definition parser  ////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

static void free_object (nsmbw_obj_t *obj)
{
	if (!obj) return;
	for (uint r = 0; r < obj->n_rows; r++)
		FREE (obj->rows[r].tiles);
	FREE (obj->rows);
}

static enumError parse_objects (nsmbw_tileset_t *ts,
	const u8 *unt, uint unt_size,
	const u8 *unt_hd, uint unt_hd_size)
{
	if (!unt || !unt_hd || unt_hd_size < 4)
		return ERR_OK;

	uint n_obj = unt_hd_size / 4;
	ts->objects = CALLOC (n_obj, sizeof (nsmbw_obj_t));
	if (!ts->objects)
		return ERR_OUT_OF_MEMORY;
	ts->n_objects = n_obj;

	for (uint o = 0; o < n_obj; o++)
	{
		const u8 *meta = unt_hd + o * 4;
		uint offset = (uint)meta[0] << 8 | meta[1];
		uint width  = meta[2];
		uint height = meta[3];

		nsmbw_obj_t *obj = &ts->objects[o];
		obj->width  = width;
		obj->height = height;

		// Parse object definition stream starting at unt[offset].
		// Rows are separated by 0xFE.  Object ends at 0xFF.
		// Slope markers have bit 7 set (0x80-0xFE range excluding 0xFE).
		// Tile entries are 3 bytes: flags, tile, slot.

		// First pass: count rows.
		uint n_rows = 1;
		for (uint i = offset; i < unt_size && unt[i] != 0xFF; i++)
			if (unt[i] == 0xFE) n_rows++;

		obj->rows = CALLOC (n_rows, sizeof (nsmbw_obj_row_t));
		if (!obj->rows)
			continue;
		obj->n_rows = n_rows;

		uint row_idx = 0;
		uint i = offset;
		uint upper_slope = 0, lower_slope = 0;

		// Pre-allocate tile array for current row (realloc as needed)
		uint row_alloc = 8;
		obj->rows[0].tiles = CALLOC (row_alloc, sizeof (nsmbw_obj_tile_t));

		while (i < unt_size && unt[i] != 0xFF)
		{
			u8 byte = unt[i];
			if (byte == 0xFE)
			{
				// End of row
				row_idx++;
				if (row_idx < n_rows)
				{
					row_alloc = 8;
					obj->rows[row_idx].tiles = CALLOC (row_alloc, sizeof (nsmbw_obj_tile_t));
				}
				i++;
			}
			else if (byte & 0x80)
			{
				// Slope marker
				if (!upper_slope)
					upper_slope = obj->upper_slope = byte;
				else
					lower_slope = obj->lower_slope = byte;
				i++;
			}
			else
			{
				// Tile entry: 3 bytes
				if (i + 2 >= unt_size)
					break;
				if (row_idx < n_rows)
				{
					nsmbw_obj_row_t *row = &obj->rows[row_idx];
					if (row->n_tiles >= row_alloc)
					{
						row_alloc *= 2;
						nsmbw_obj_tile_t *nr = REALLOC (row->tiles,
							row_alloc * sizeof (nsmbw_obj_tile_t));
						if (nr)
							row->tiles = nr;
					}
					if (row->n_tiles < row_alloc)
					{
						row->tiles[row->n_tiles].flags = unt[i];
						row->tiles[row->n_tiles].tile  = unt[i + 1];
						row->tiles[row->n_tiles].slot  = unt[i + 2];
						row->n_tiles++;
					}
				}
				i += 3;
			}
		}
		(void)upper_slope;
		(void)lower_slope;
	}

	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
////////////////////////  ScanNSMBWTileset  ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

enumError ScanNSMBWTileset (nsmbw_tileset_t *ts,
	const u8 *tex_lz, uint tex_lz_size,
	const u8 *chk, uint chk_size,
	const u8 *unt, uint unt_size,
	const u8 *unt_hd, uint unt_hd_size)
{
	memset (ts, 0, sizeof (*ts));

	// Decompress texture
	if (tex_lz && tex_lz_size > 0)
	{
		enumError err = DecodeLZ10LZ11 (&ts->tex_raw, &ts->tex_raw_size,
			tex_lz, tex_lz_size);
		if (!err && ts->tex_raw && ts->tex_raw_size >= 524288)
		{
			ts->argb = MALLOC (1024 * 256 * 4);
			if (ts->argb)
				DecodeRGB4A3 (ts->argb, ts->tex_raw, ts->tex_raw_size);
		}
	}

	// Behaviour table
	if (chk && chk_size >= 2048)
	{
		memcpy (ts->beh, chk, 2048);
		ts->have_beh = true;
	}

	// Object definitions
	parse_objects (ts, unt, unt_size, unt_hd, unt_hd_size);

	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
////////////////////////  ResetNSMBWTileset  //////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void ResetNSMBWTileset (nsmbw_tileset_t *ts)
{
	if (!ts) return;
	FREE (ts->tex_raw);
	FREE (ts->argb);
	if (ts->objects)
	{
		for (uint i = 0; i < ts->n_objects; i++)
			free_object (&ts->objects[i]);
		FREE (ts->objects);
	}
	memset (ts, 0, sizeof (*ts));
}

//
///////////////////////////////////////////////////////////////////////////////
/////////////////////  DumpNSMBWBehaviour  ////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// Behaviour bit meanings based on Puzzle-Updated documentation.
static const char *beh_byte0_flags[] = {
	"solid",           // 0x01
	"solid_top",       // 0x02
	"slope",           // 0x04
	"spike",           // 0x08
	"climbable",       // 0x10
	"lava",            // 0x20
	"passthrough",     // 0x40
	"quicksand",       // 0x80
};

enumError DumpNSMBWBehaviour (const nsmbw_tileset_t *ts,
	char **out, size_t *out_size, ccp tileset_name)
{
	if (!ts || !out)
		return ERR_MISSING_PARAM;

	nsmbw_sb_t s;
	nsb_init (&s);

	nsb_printf (&s, "# NSMBW Tileset Behaviour Table: %s\n", tileset_name ? tileset_name : "?");
	nsb_puts (&s, "# 256 tiles, 8 bytes each (big-endian)\n");
	nsb_puts (&s, "# Format: tile_idx [byte0 byte1 byte2 byte3 byte4 byte5 byte6 byte7]  flags\n\n");

	for (uint i = 0; i < 256; i++)
	{
		const u8 *b = ts->beh[i].byte;
		nsb_printf (&s, "tile %3u  [%02X %02X %02X %02X %02X %02X %02X %02X]",
			i, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);

		// Decode byte 0 flags
		bool any = false;
		for (uint bit = 0; bit < 8; bit++)
		{
			if (b[0] & (1u << bit))
			{
				nsb_putc (&s, any ? ',' : ' ');
				nsb_puts (&s, beh_byte0_flags[bit]);
				any = true;
			}
		}
		if (!any)
			nsb_puts (&s, " (empty)");
		nsb_putc (&s, '\n');
	}

	if (!s.buf)
	{
		*out = NULL;
		if (out_size) *out_size = 0;
		return ERR_OUT_OF_MEMORY;
	}
	*out = s.buf;
	if (out_size) *out_size = s.len;
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
/////////////////////  DumpNSMBWObjects  //////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

enumError DumpNSMBWObjects (const nsmbw_tileset_t *ts,
	char **out, size_t *out_size, ccp tileset_name)
{
	if (!ts || !out)
		return ERR_MISSING_PARAM;

	nsmbw_sb_t s;
	nsb_init (&s);

	nsb_printf (&s, "# NSMBW Object Definitions: %s\n", tileset_name ? tileset_name : "?");
	nsb_printf (&s, "# %u objects\n\n", ts->n_objects);

	for (uint o = 0; o < ts->n_objects; o++)
	{
		const nsmbw_obj_t *obj = &ts->objects[o];
		nsb_printf (&s, "OBJECT %u  %ux%u", o, obj->width, obj->height);
		if (obj->upper_slope)
			nsb_printf (&s, "  upper_slope=0x%02X", obj->upper_slope);
		if (obj->lower_slope)
			nsb_printf (&s, "  lower_slope=0x%02X", obj->lower_slope);
		nsb_putc (&s, '\n');

		for (uint r = 0; r < obj->n_rows; r++)
		{
			const nsmbw_obj_row_t *row = &obj->rows[r];
			nsb_printf (&s, "  ROW %u:", r);
			for (uint t = 0; t < row->n_tiles; t++)
			{
				nsb_printf (&s, " (f=%02X,tile=%u,Pa%u)",
					row->tiles[t].flags,
					row->tiles[t].tile,
					row->tiles[t].slot);
			}
			nsb_putc (&s, '\n');
		}
		nsb_putc (&s, '\n');
	}

	if (!s.buf)
	{
		*out = NULL;
		if (out_size) *out_size = 0;
		return ERR_OUT_OF_MEMORY;
	}
	*out = s.buf;
	if (out_size) *out_size = s.len;
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
/////////////////////  ExportNSMBWTexARGB  ////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

enumError ExportNSMBWTexARGB (const nsmbw_tileset_t *ts,
	u8 **out, size_t *out_size)
{
	if (!ts || !out)
		return ERR_MISSING_PARAM;
	if (!ts->argb)
		return ERR_INVALID_DATA;

	size_t sz = 1024u * 256u * 4u;
	u8 *buf = MALLOC (sz);
	if (!buf)
		return ERR_OUT_OF_MEMORY;
	memcpy (buf, ts->argb, sz);
	*out = buf;
	if (out_size) *out_size = sz;
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
/////////////////////  EncodeNSMBWTexLZ  //////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

enumError EncodeNSMBWTexLZ (const nsmbw_tileset_t *ts,
	u8 **out, uint *out_size)
{
	if (!ts || !out || !ts->argb)
		return ERR_MISSING_PARAM;

	// Encode RGBA8 back to RGB4A3
	u8 *raw = MALLOC (524288);
	if (!raw)
		return ERR_OUT_OF_MEMORY;
	EncodeRGB4A3 (raw, ts->argb);

	// Compress with LZ11
	u8 *lz = NULL;
	uint lz_size = 0;
	enumError err = EncodeLZ10LZ11 (&lz, &lz_size, raw, 524288, true /*lz11*/);
	FREE (raw);
	if (err)
		return err;

	*out = lz;
	if (out_size) *out_size = lz_size;
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////////  ExtractNSMBWTilesetArc  //////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

enumError ExtractNSMBWTilesetArc (ccp arc_path, ccp dest_dir)
{
	(void)arc_path;
	(void)dest_dir;
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
/////////    Newer SMBW LevelInfo.bin (NWRp) Implementation         //////////
///////////////////////////////////////////////////////////////////////////////

bool IsNWRLevelInfo (const u8 *data, size_t size)
{
	if (!data || size < 8)
		return false;
	return !memcmp (data, NWRP_MAGIC, 4);
}

enumError ScanNWRLevelInfo (nwr_levelinfo_t *li, const u8 *data, size_t size)
{
	memset (li, 0, sizeof (*li));
	if (!data || size < 8 || memcmp (data, NWRP_MAGIC, 4) != 0)
		return ERR_INVALID_DATA;

	uint num_worlds = be32 (data + 4);
	if (8 + num_worlds * 4 > size)
		return ERR_INVALID_DATA;

	li->worlds = CALLOC (num_worlds, sizeof (nwr_world_t));
	if (!li->worlds)
		return ERR_OUT_OF_MEMORY;
	li->n_worlds = num_worlds;

	uint min_text_offs = 0xFFFFFFFF;
	uint max_struct_offs = 8 + num_worlds * 4;

	for (uint w = 0; w < num_worlds; w++)
	{
		uint world_off = be32 (data + 8 + w * 4);
		if (world_off + 4 > size)
			continue;

		uint num_entries = be32 (data + world_off);
		if (world_off + 4 + num_entries * 12 > size)
			continue;

		if (world_off + 4 + num_entries * 12 > max_struct_offs)
			max_struct_offs = world_off + 4 + num_entries * 12;

		nwr_world_t *world = &li->worlds[w];
		world->levels = CALLOC (num_entries, sizeof (nwr_level_entry_t));
		if (!world->levels)
			continue;

		for (uint l = 0; l < num_entries; l++)
		{
			const u8 *ep = data + world_off + 4 + l * 12;
			u8 f_world = ep[0];
			u8 f_level = ep[1];
			u8 d_world = ep[2];
			u8 d_level = ep[3];
			u8 t_len   = ep[4];
			u16 flags  = be16 (ep + 6);
			u32 t_off  = be32 (ep + 8);

			if (t_off < min_text_offs)
				min_text_offs = t_off;

			// Decode shifted text
			char *name = MALLOC (t_len + 1);
			if (name)
			{
				for (uint c = 0; c < t_len && t_off + c < size; c++)
					name[c] = (char)((data[t_off + c] + 0x30) & 0xFF);
				name[t_len] = 0;
			}

			if (d_level >= 100)
			{
				// World header
				world->world_number = d_world;
				if (d_level == 100)
				{
					world->has_left = true;
					world->name_left = name;
				}
				else
				{
					world->has_right = true;
					world->name_right = name;
				}
			}
			else
			{
				nwr_level_entry_t *lvl = &world->levels[world->n_levels++];
				lvl->file_world = f_world + 1; // 1-indexed
				lvl->file_level = f_level + 1;
				lvl->display_world = d_world;
				lvl->display_level = d_level;
				lvl->flags = flags;
				lvl->name = name;
			}
		}
	}

	// Read comments if available
	if (min_text_offs > max_struct_offs && max_struct_offs < size)
	{
		uint c_len = min_text_offs - max_struct_offs;
		li->comments = MALLOC (c_len + 1);
		if (li->comments)
		{
			memcpy (li->comments, data + max_struct_offs, c_len);
			li->comments[c_len] = 0;
		}
	}

	return ERR_OK;
}

void ResetNWRLevelInfo (nwr_levelinfo_t *li)
{
	if (!li) return;
	FREE (li->comments);
	if (li->worlds)
	{
		for (uint w = 0; w < li->n_worlds; w++)
		{
			nwr_world_t *world = &li->worlds[w];
			FREE (world->name_left);
			FREE (world->name_right);
			if (world->levels)
			{
				for (uint l = 0; l < world->n_levels; l++)
					FREE (world->levels[l].name);
				FREE (world->levels);
			}
		}
		FREE (li->worlds);
	}
	memset (li, 0, sizeof (*li));
}

enumError DumpNWRLevelInfoText (const nwr_levelinfo_t *li, char **out, size_t *out_size)
{
	if (!li || !out)
		return ERR_MISSING_PARAM;

	nsmbw_sb_t s;
	nsb_init (&s);

	nsb_puts (&s, "# Newer Super Mario Bros. Wii Level Information (LevelInfo.bin)\n");
	if (li->comments && *li->comments)
		nsb_printf (&s, "# Comments: %s\n", li->comments);
	nsb_printf (&s, "# Worlds: %u\n\n", li->n_worlds);

	for (uint w = 0; w < li->n_worlds; w++)
	{
		const nwr_world_t *world = &li->worlds[w];
		nsb_printf (&s, "[WORLD %u]\n", world->world_number ? world->world_number : (w + 1));
		if (world->has_left)
			nsb_printf (&s, "name_left  = \"%s\"\n", world->name_left ? world->name_left : "");
		if (world->has_right)
			nsb_printf (&s, "name_right = \"%s\"\n", world->name_right ? world->name_right : "");

		for (uint l = 0; l < world->n_levels; l++)
		{
			const nwr_level_entry_t *lvl = &world->levels[l];
			nsb_printf (&s, "  level %u-%u (file: %02u-%02u, flags: 0x%04X) = \"%s\"\n",
				lvl->display_world, lvl->display_level,
				lvl->file_world, lvl->file_level,
				lvl->flags, lvl->name ? lvl->name : "");
		}
		nsb_putc (&s, '\n');
	}

	if (!s.buf)
	{
		*out = NULL;
		if (out_size) *out_size = 0;
		return ERR_OUT_OF_MEMORY;
	}
	*out = s.buf;
	if (out_size) *out_size = s.len;
	return ERR_OK;
}

enumError CreateNWRLevelInfo (u8 **dest, size_t *dest_size, const nwr_levelinfo_t *li)
{
	if (!dest || !li)
		return ERR_MISSING_PARAM;

	// Calculate structure size
	uint num_worlds = li->n_worlds;
	uint comments_len = li->comments ? (uint)strlen (li->comments) : 0;

	uint offset = 4 + 4; // "NWRp" + num_worlds
	for (uint w = 0; w < num_worlds; w++)
	{
		offset += 4; // world offset
		offset += 4; // num levels
		const nwr_world_t *world = &li->worlds[w];
		if (world->has_left) offset += 12;
		if (world->has_right) offset += 12;
		offset += world->n_levels * 12;
	}

	uint text_start = offset + comments_len + 1;

	// Build buffer
	uint cap = text_start + 4096;
	u8 *buf = MALLOC (cap);
	if (!buf)
		return ERR_OUT_OF_MEMORY;

	memcpy (buf, NWRP_MAGIC, 4);
	buf[4] = (num_worlds >> 24) & 0xFF;
	buf[5] = (num_worlds >> 16) & 0xFF;
	buf[6] = (num_worlds >>  8) & 0xFF;
	buf[7] = (num_worlds      ) & 0xFF;

	uint cur_world_off = 8 + num_worlds * 4;
	uint cur_text_off = text_start;

	// Temporary buffer for text
	uint text_cap = 4096, text_len = 0;
	u8 *text_buf = MALLOC (text_cap);

	for (uint w = 0; w < num_worlds; w++)
	{
		const nwr_world_t *world = &li->worlds[w];
		uint world_off_slot = 8 + w * 4;
		buf[world_off_slot + 0] = (cur_world_off >> 24) & 0xFF;
		buf[world_off_slot + 1] = (cur_world_off >> 16) & 0xFF;
		buf[world_off_slot + 2] = (cur_world_off >>  8) & 0xFF;
		buf[world_off_slot + 3] = (cur_world_off      ) & 0xFF;

		uint total_entries = world->n_levels;
		if (world->has_left) total_entries++;
		if (world->has_right) total_entries++;

		buf[cur_world_off + 0] = (total_entries >> 24) & 0xFF;
		buf[cur_world_off + 1] = (total_entries >> 16) & 0xFF;
		buf[cur_world_off + 2] = (total_entries >>  8) & 0xFF;
		buf[cur_world_off + 3] = (total_entries      ) & 0xFF;
		cur_world_off += 4;

		// Write world left header if present
		if (world->has_left)
		{
			ccp name = world->name_left ? world->name_left : "";
			uint nlen = (uint)strlen (name);
			u8 *ep = buf + cur_world_off;
			ep[0] = 98; ep[1] = 98;
			ep[2] = world->world_number; ep[3] = 100;
			ep[4] = nlen; ep[5] = 0;
			ep[6] = 0; ep[7] = 0; // flags
			ep[8] = (cur_text_off >> 24) & 0xFF;
			ep[9] = (cur_text_off >> 16) & 0xFF;
			ep[10] = (cur_text_off >> 8) & 0xFF;
			ep[11] = (cur_text_off     ) & 0xFF;
			cur_world_off += 12;

			for (uint c = 0; c < nlen; c++)
			{
				if (text_len + 2 > text_cap) { text_cap *= 2; text_buf = REALLOC (text_buf, text_cap); }
				text_buf[text_len++] = (u8)((name[c] - 0x30) & 0xFF);
			}
			text_buf[text_len++] = 0;
			cur_text_off += nlen + 1;
		}

		// Write world right header if present
		if (world->has_right)
		{
			ccp name = world->name_right ? world->name_right : "";
			uint nlen = (uint)strlen (name);
			u8 *ep = buf + cur_world_off;
			ep[0] = 98; ep[1] = 98;
			ep[2] = world->world_number; ep[3] = 101;
			ep[4] = nlen; ep[5] = 0;
			ep[6] = 0x04; ep[7] = 0; // flags (0x0400 = right)
			ep[8] = (cur_text_off >> 24) & 0xFF;
			ep[9] = (cur_text_off >> 16) & 0xFF;
			ep[10] = (cur_text_off >> 8) & 0xFF;
			ep[11] = (cur_text_off     ) & 0xFF;
			cur_world_off += 12;

			for (uint c = 0; c < nlen; c++)
			{
				if (text_len + 2 > text_cap) { text_cap *= 2; text_buf = REALLOC (text_buf, text_cap); }
				text_buf[text_len++] = (u8)((name[c] - 0x30) & 0xFF);
			}
			text_buf[text_len++] = 0;
			cur_text_off += nlen + 1;
		}

		// Write level entries
		for (uint l = 0; l < world->n_levels; l++)
		{
			const nwr_level_entry_t *lvl = &world->levels[l];
			ccp name = lvl->name ? lvl->name : "";
			uint nlen = (uint)strlen (name);
			u8 *ep = buf + cur_world_off;
			ep[0] = (lvl->file_world > 0 ? lvl->file_world - 1 : 0);
			ep[1] = (lvl->file_level > 0 ? lvl->file_level - 1 : 0);
			ep[2] = lvl->display_world;
			ep[3] = lvl->display_level;
			ep[4] = nlen;
			ep[5] = 0;
			ep[6] = (lvl->flags >> 8) & 0xFF;
			ep[7] = (lvl->flags     ) & 0xFF;
			ep[8] = (cur_text_off >> 24) & 0xFF;
			ep[9] = (cur_text_off >> 16) & 0xFF;
			ep[10] = (cur_text_off >> 8) & 0xFF;
			ep[11] = (cur_text_off     ) & 0xFF;
			cur_world_off += 12;

			for (uint c = 0; c < nlen; c++)
			{
				if (text_len + 2 > text_cap) { text_cap *= 2; text_buf = REALLOC (text_buf, text_cap); }
				text_buf[text_len++] = (u8)((name[c] - 0x30) & 0xFF);
			}
			text_buf[text_len++] = 0;
			cur_text_off += nlen + 1;
		}
	}

	// Write comments
	if (li->comments && *li->comments)
		memcpy (buf + cur_world_off, li->comments, comments_len);
	buf[cur_world_off + comments_len] = 0;

	// Append text
	if (text_start + text_len > cap)
	{
		cap = text_start + text_len + 64;
		buf = REALLOC (buf, cap);
	}
	memcpy (buf + text_start, text_buf, text_len);
	FREE (text_buf);

	*dest = buf;
	if (dest_size) *dest_size = text_start + text_len;
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
/////////    Newer SMBW AnimTiles.bin (NWRa) Implementation         //////////
///////////////////////////////////////////////////////////////////////////////

bool IsNWRAnimTiles (const u8 *data, size_t size)
{
	if (!data || size < 8)
		return false;
	return !memcmp (data, NWRA_MAGIC, 4);
}

enumError ScanNWRAnimTiles (nwr_animtiles_t *at, const u8 *data, size_t size)
{
	memset (at, 0, sizeof (*at));
	if (!data || size < 8 || memcmp (data, NWRA_MAGIC, 4) != 0)
		return ERR_INVALID_DATA;

	uint num_entries = be32 (data + 4);
	if (8 + num_entries * 8 > size)
		return ERR_INVALID_DATA;

	at->entries = CALLOC (num_entries, sizeof (nwr_animtile_entry_t));
	if (!at->entries)
		return ERR_OUT_OF_MEMORY;
	at->n_entries = num_entries;

	for (uint i = 0; i < num_entries; i++)
	{
		const u8 *ep = data + 8 + i * 8;
		u16 tex_name_off = be16 (ep + 0);
		u16 delay_off    = be16 (ep + 2);
		u16 tile_num     = be16 (ep + 4);
		u8 tileset_num   = ep[6];
		u8 reverse       = ep[7];

		nwr_animtile_entry_t *entry = &at->entries[i];
		entry->tile_num = tile_num;
		entry->tileset_num = tileset_num;
		entry->reverse = reverse;

		if (tex_name_off < size)
		{
			size_t nlen = strnlen ((const char*)data + tex_name_off, size - tex_name_off);
			entry->tex_name = MALLOC (nlen + 1);
			if (entry->tex_name)
			{
				memcpy (entry->tex_name, data + tex_name_off, nlen);
				entry->tex_name[nlen] = 0;
			}
		}

		if (delay_off < size)
		{
			size_t dlen = strnlen ((const char*)data + delay_off, size - delay_off);
			entry->frame_delays = MALLOC (dlen + 1);
			if (entry->frame_delays)
			{
				memcpy (entry->frame_delays, data + delay_off, dlen);
				entry->frame_delays[dlen] = 0;
			}
		}
	}

	return ERR_OK;
}

void ResetNWRAnimTiles (nwr_animtiles_t *at)
{
	if (!at) return;
	if (at->entries)
	{
		for (uint i = 0; i < at->n_entries; i++)
		{
			FREE (at->entries[i].tex_name);
			FREE (at->entries[i].frame_delays);
		}
		FREE (at->entries);
	}
	memset (at, 0, sizeof (*at));
}

enumError DumpNWRAnimTilesText (const nwr_animtiles_t *at, char **out, size_t *out_size)
{
	if (!at || !out)
		return ERR_MISSING_PARAM;

	nsmbw_sb_t s;
	nsb_init (&s);

	nsb_puts (&s, "# Newer Super Mario Bros. Wii Animated Tiles (AnimTiles.bin)\n");
	nsb_printf (&s, "# Entries: %u\n\n", at->n_entries);
	nsb_puts (&s, "# tile_num  tileset  reverse  texture_name                 frame_delays\n");

	for (uint i = 0; i < at->n_entries; i++)
	{
		const nwr_animtile_entry_t *e = &at->entries[i];
		nsb_printf (&s, "0x%04X      Pa%u      %d        %-28s %s\n",
			e->tile_num, e->tileset_num, e->reverse,
			e->tex_name ? e->tex_name : "",
			e->frame_delays ? e->frame_delays : "");
	}

	if (!s.buf)
	{
		*out = NULL;
		if (out_size) *out_size = 0;
		return ERR_OUT_OF_MEMORY;
	}
	*out = s.buf;
	if (out_size) *out_size = s.len;
	return ERR_OK;
}

enumError CreateNWRAnimTiles (u8 **dest, size_t *dest_size, const nwr_animtiles_t *at)
{
	if (!dest || !at)
		return ERR_MISSING_PARAM;

	uint num_entries = at->n_entries;
	uint header_size = 8 + num_entries * 8;
	uint cap = header_size + 4096;
	u8 *buf = MALLOC (cap);
	if (!buf)
		return ERR_OUT_OF_MEMORY;

	memcpy (buf, NWRA_MAGIC, 4);
	buf[4] = (num_entries >> 24) & 0xFF;
	buf[5] = (num_entries >> 16) & 0xFF;
	buf[6] = (num_entries >>  8) & 0xFF;
	buf[7] = (num_entries      ) & 0xFF;

	uint cur_str_off = header_size;
	for (uint i = 0; i < num_entries; i++)
	{
		const nwr_animtile_entry_t *e = &at->entries[i];
		ccp tex = e->tex_name ? e->tex_name : "";
		ccp del = e->frame_delays ? e->frame_delays : "";
		uint tlen = (uint)strlen (tex);
		uint dlen = (uint)strlen (del);

		if (cur_str_off + tlen + dlen + 4 > cap)
		{
			cap = (cap * 2) + tlen + dlen + 64;
			buf = REALLOC (buf, cap);
		}

		uint tex_off = cur_str_off;
		memcpy (buf + cur_str_off, tex, tlen + 1);
		cur_str_off += tlen + 1;

		uint del_off = cur_str_off;
		memcpy (buf + cur_str_off, del, dlen + 1);
		cur_str_off += dlen + 1;

		u8 *ep = buf + 8 + i * 8;
		ep[0] = (tex_off >> 8) & 0xFF;
		ep[1] = (tex_off     ) & 0xFF;
		ep[2] = (del_off >> 8) & 0xFF;
		ep[3] = (del_off     ) & 0xFF;
		ep[4] = (e->tile_num >> 8) & 0xFF;
		ep[5] = (e->tile_num     ) & 0xFF;
		ep[6] = e->tileset_num;
		ep[7] = e->reverse;
	}

	*dest = buf;
	if (dest_size) *dest_size = cur_str_off;
	return ERR_OK;
}

bool IsNSMBWChk (const u8 *data, size_t size)
{
	// 256 entries * 8 bytes = 2048 bytes
	return data && size == 2048;
}

