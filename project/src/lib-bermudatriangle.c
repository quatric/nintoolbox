// SPDX-License-Identifier: GPL-2.0+
// "Planet-G" Wii engine formats (Bermuda Triangle, Rock Blast, Veggy World, Ocean Commander, ...) -- see
// lib-bermudatriangle.h for the layouts and how they were checked.

#include "lib-bermudatriangle.h"
#include "lib-nintendo.h"
#include <string.h>
#include <ctype.h>

//-----------------------------------------------------------------------------
// shared helpers

static void print_escaped (FILE *f, const u8 *data, size_t len)
{
	for (size_t i = 0; i < len; i++)
	{
		u8 c = data[i];
		if (c == '\\')
			fputs ("\\\\", f);
		else if (c >= 0x20 && c < 0x7f)
			fputc (c, f);
		else
			fprintf (f, "\\x%02x", c);
	}
}

static int is_ascii_run (const u8 *data, size_t len)
{
	if (!len)
		return 0;
	for (size_t i = 0; i < len; i++)
		if (!isprint (data[i]) && data[i] != '\t')
			return 0;
	return 1;
}

//-----------------------------------------------------------------------------
// (0) ".MWT" "GDATAVERSION" outer resource envelope

enum
{
	BT_MWT_MAGIC_LEN = 12,
	BT_MWT_HEADER_SIZE = 0x40
};
#define BT_MWT_MAGIC "GDATAVERSION"
#define BT_TPL_MAGIC 0x0020af30u // big endian on disk: 00 20 af 30

// Size of the optional 1-bit hit mask behind the TPL.
static u64 mwt_mask_size (const u8 *data)
{
	return rd_le32 (data + 0x1c) & 1 ? (u64)((rd_le32 (data + 0x14) + 7) / 8) * rd_le32 (data + 0x18) : 0;
}

int IsBermudaMwt (const u8 *data, size_t size, size_t file_size)
{
	if (file_size && size > file_size)
		size = file_size; // bytes past the end of the file are not part of it
	if (!data || size < BT_MWT_HEADER_SIZE + 4)
		return 0;
	if (memcmp (data, BT_MWT_MAGIC, BT_MWT_MAGIC_LEN) || rd_le32 (data + BT_MWT_MAGIC_LEN) != 2)
		return 0;
	const u32 width = rd_le32 (data + 0x14), height = rd_le32 (data + 0x18);
	if (!width || width > 8192 || !height || height > 8192 || rd_le32 (data + 0x1c) > 1)
		return 0;
	for (uint i = 0x20; i < BT_MWT_HEADER_SIZE; i++)
		if (data[i])
			return 0;
	if (rd_be32 (data + BT_MWT_HEADER_SIZE) != BT_TPL_MAGIC)
		return 0;
	return !file_size
		|| file_size == BT_MWT_HEADER_SIZE + (u64)rd_le32 (data + 0x10) + mwt_mask_size (data);
}

enumError DecodeBermudaMwt_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsBermudaMwt (data, size, file_size))
		return EINVAL;

	u32 payload_size = rd_le32 (data + 0x10);
	u32 width = rd_le32 (data + 0x14);
	u32 height = rd_le32 (data + 0x18);
	u32 flags = rd_le32 (data + 0x1c);

	fprintf (f, "# Planet-G GDATAVERSION texture envelope (.MWT)\n");
	fprintf (f, "version = 2\n");
	fprintf (f, "payload_size = %u  # TPL following this 0x%x-byte header\n", payload_size,
		BT_MWT_HEADER_SIZE);
	fprintf (f, "width = %u\n", width);
	fprintf (f, "height = %u\n", height);
	fprintf (f, "flags = %u%s\n", flags, flags & 1 ? "  # 1-bit hit mask follows the TPL" : "");
	if (flags & 1)
		fprintf (f, "mask_size = %llu  # ceil(width / 8) * height bytes\n",
			(unsigned long long)mwt_mask_size (data));

	if ((u64)BT_MWT_HEADER_SIZE + payload_size > size)
	{
		fprintf (f, "# payload truncated/out-of-bounds -- not inspected further\n");
		return ERR_OK;
	}

	const u8 *payload = data + BT_MWT_HEADER_SIZE;
	if (payload_size >= 4 && rd_be32 (payload) == BT_TPL_MAGIC)
		fprintf (f, "payload = TPL texture (magic 00 20 af 30)\n");
	else
		fprintf (f, "payload = unrecognized (%u bytes)\n", payload_size);

	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (1) ".MWG" / ".MSP" "PLANETG" tagged-object serialization

// Root type: "PLANETG_" + [A-Z0-9_]+ (PLANETG_SPRITECOLLECTION, PLANETG_SPRITE), then a small version.
int IsBermudaPlanetG (const u8 *data, size_t size, size_t file_size)
{
	if (file_size && size > file_size)
		size = file_size; // bytes past the end of the file are not part of it
	(void)file_size;
	if (!data || size < 8)
		return 0;
	const u32 len = rd_le32 (data);
	if (len < 9 || len > 64 || 8 + (u64)len > size || memcmp (data + 4, "PLANETG_", 8))
		return 0;
	for (u32 i = 8; i < len; i++)
	{
		const u8 c = data[4 + i];
		if (!isupper (c) && !isdigit (c) && c != '_')
			return 0;
	}
	const u32 version = rd_le32 (data + 4 + len);
	return version >= 1 && version <= 16;
}

// Structured reader following the game's own readers (see note (1) in the header). With f == NULL it
// only validates; every read is bounds checked and a failure clears c->ok.
typedef struct
{
	const u8 *d;
	size_t n, p;
	int ok;
	FILE *f;
} pg_cursor;

static u32 pg_u32 (pg_cursor *c)
{
	if (!c->ok || c->n - c->p < 4)
		return c->ok = 0;
	const u32 v = rd_le32 (c->d + c->p);
	c->p += 4;
	return v;
}

static double pg_f32 (pg_cursor *c)
{
	union { u32 u; float f; } v = { pg_u32 (c) };
	return v.f;
}

// u32 element count; each element takes at least 4 bytes, which bounds the loops on corrupt input
static u32 pg_count (pg_cursor *c)
{
	const u32 n = pg_u32 (c);
	if (c->ok && n > (c->n - c->p) / 4)
		c->ok = 0;
	return c->ok ? n : 0;
}

static void pg_str (pg_cursor *c, const char *prefix)
{
	const u32 len = pg_u32 (c);
	if (!c->ok || len > c->n - c->p)
	{
		c->ok = 0;
		return;
	}
	if (c->f)
	{
		fprintf (c->f, "%s\"", prefix);
		print_escaped (c->f, c->d + c->p, len);
		fputc ('"', c->f);
	}
	c->p += len;
}

// u32 count + count x f32, printed on one line
static void pg_floats (pg_cursor *c, int ind, const char *name)
{
	const u32 n = pg_count (c);
	if (c->f)
		fprintf (c->f, "%*s%s =", ind, "", name);
	for (u32 i = 0; i < n && c->ok; i++)
	{
		const double v = pg_f32 (c);
		if (c->f)
			fprintf (c->f, " %g", v);
	}
	if (c->f)
		fputc ('\n', c->f);
}

// SpritePicture: path, u32 colour (read and dropped by the Wii build), u32 flag (low byte kept),
// u32 has_rect + 4 x s32 LTRB active rect, two u32 stored as s16 (-1 = unset).
static void pg_picture (pg_cursor *c, int ind, u32 idx)
{
	if (c->f)
		fprintf (c->f, "%*spicture[%u] ", ind, "", idx);
	pg_str (c, "");
	const u32 colour = pg_u32 (c), flag = pg_u32 (c);
	const int has_rect = pg_u32 (c) == 1;
	s32 rect[4] = { -1, -1, -1, -1 };
	for (int i = 0; has_rect && i < 4; i++)
		rect[i] = (s32)pg_u32 (c);
	const s32 w = (s32)pg_u32 (c), h = (s32)pg_u32 (c);
	if (c->f && c->ok)
	{
		fprintf (c->f, " colour=0x%06x flag=%u size=%d,%d", colour, flag & 0xff, w, h);
		if (has_rect)
			fprintf (c->f, " rect=%d,%d,%d,%d", rect[0], rect[1], rect[2], rect[3]);
		fputc ('\n', c->f);
	}
}

// SpriteObj: name, picture keys, their times, position keys (optional vec2), their times,
// scalar keys (optional f32), their times, 5 u32 flags.
static void pg_object (pg_cursor *c, int ind, u32 idx)
{
	if (c->f)
		fprintf (c->f, "%*sobject[%u] ", ind, "", idx);
	pg_str (c, "");
	if (c->f)
		fputc ('\n', c->f);
	ind += 2;

	u32 n = pg_count (c);
	for (u32 i = 0; i < n && c->ok; i++)
		if (pg_u32 (c))
			pg_picture (c, ind, i);
		else if (c->f)
			fprintf (c->f, "%*spicture[%u] none\n", ind, "", i);
	pg_floats (c, ind, "picture_times");

	n = pg_count (c);
	for (u32 i = 0; i < n && c->ok; i++)
		if (pg_u32 (c))
		{
			const double x = pg_f32 (c), y = pg_f32 (c);
			if (c->f)
				fprintf (c->f, "%*sposition[%u] = %g %g\n", ind, "", i, x, y);
		}
		else if (c->f)
			fprintf (c->f, "%*sposition[%u] none\n", ind, "", i);
	pg_floats (c, ind, "position_times");

	n = pg_count (c);
	for (u32 i = 0; i < n && c->ok; i++)
		if (pg_u32 (c))
		{
			const double v = pg_f32 (c);
			if (c->f)
				fprintf (c->f, "%*svalue[%u] = %g\n", ind, "", i, v);
		}
		else if (c->f)
			fprintf (c->f, "%*svalue[%u] none\n", ind, "", i);
	pg_floats (c, ind, "value_times");

	if (c->f)
		fprintf (c->f, "%*sflags =", ind, "");
	for (int i = 0; i < 5; i++)
	{
		const u32 v = pg_u32 (c);
		if (c->f)
			fprintf (c->f, " %u", v);
	}
	if (c->f)
		fputc ('\n', c->f);
}

// Sprite: name, u32 animation count, per animation a name and an array of optional objects.
static void pg_sprite (pg_cursor *c, int ind)
{
	if (c->f)
		fprintf (c->f, "%*ssprite ", ind, "");
	pg_str (c, "");
	const u32 anims = pg_count (c);
	if (c->f)
		fprintf (c->f, " animations=%u\n", anims);
	for (u32 a = 0; a < anims && c->ok; a++)
	{
		if (c->f)
			fprintf (c->f, "%*sanimation ", ind + 2, "");
		pg_str (c, "");
		const u32 n = pg_count (c);
		if (c->f)
			fprintf (c->f, " objects=%u\n", n);
		for (u32 i = 0; i < n && c->ok; i++)
			if (pg_u32 (c))
				pg_object (c, ind + 4, i);
			else if (c->f)
				fprintf (c->f, "%*sobject[%u] none\n", ind + 4, "", i);
	}
}

// Root: type string, u32 version, then a SpriteCollection (u32 count + optional sprites) or one Sprite.
static int pg_root (pg_cursor *c)
{
	const u32 len = rd_le32 (c->d);
	const int coll = len == 24 && !memcmp (c->d + 4, "PLANETG_SPRITECOLLECTION", 24);
	if (!coll && !(len == 14 && !memcmp (c->d + 4, "PLANETG_SPRITE", 14)))
		return 0;
	c->p = 4 + len;
	const u32 version = pg_u32 (c);
	if (c->f)
		fprintf (c->f, "root = %.*s\nversion = %u\n", (int)len, c->d + 4, version);
	if (!coll)
		pg_sprite (c, 0);
	else
	{
		const u32 n = pg_count (c);
		if (c->f)
			fprintf (c->f, "sprites = %u\n", n);
		for (u32 i = 0; i < n && c->ok; i++)
			if (pg_u32 (c))
				pg_sprite (c, 2);
			else if (c->f)
				fprintf (c->f, "  sprite none\n");
	}
	return c->ok && c->p == c->n;
}

enumError DecodeBermudaPlanetG_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsBermudaPlanetG (data, size, file_size))
		return EINVAL;
	if (file_size && size > file_size)
		size = file_size;

	pg_cursor check = { data, size, 0, 1, NULL };
	if (pg_root (&check))
	{
		fprintf (f,
			"# Planet-G PLANETG sprite resource (layout of the SpriteCollection / Sprite / SpriteObj /\n"
			"# SpritePicture readers in the games' main.dol; *_times are the key times of the list above)\n");
		pg_cursor out = { data, size, 0, 1, f };
		pg_root (&out);
		return ERR_OK;
	}

	// Fallback for files that do not follow that layout: length-prefixed ASCII runs are
	// printed as strings, everything else as raw u32 fields.
	fprintf (f,
		"# Planet-G PLANETG tagged-object resource\n"
		"# not the sprite layout: heuristic walk, length-prefixed ASCII runs are tags/strings,\n"
		"# everything else is printed as a raw u32 field (see lib-bermudatriangle.h)\n");

	size_t pos = 0;
	while (pos + 4 <= size)
	{
		u32 val = rd_le32 (data + pos);
		if (val && val <= 256 && pos + 4 + (u64)val <= size && is_ascii_run (data + pos + 4, val))
		{
			fprintf (f, "tag[0x%zx] len=%u \"", pos, val);
			print_escaped (f, data + pos + 4, val);
			fputs ("\"\n", f);
			pos += 4 + val;
		}
		else
		{
			fprintf (f, "field[0x%zx] = %u (0x%x)\n", pos, val, val);
			pos += 4;
		}
	}

	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (2) ".PKI" "IMAGE_WII_COMPACT_FILE_VERSION_1" texture-pack container

enum
{
	BT_PKI_MAGIC_LEN = 32,
	BT_PKI_HEADER_SIZE = 36
};
#define BT_PKI_MAGIC "IMAGE_WII_COMPACT_FILE_VERSION_1"

// Magic, then the entry table: printable names, every entry inside the file and behind the table. Entries
// beyond the buffered bytes are not checked, but at least the first one must be buffered.
int IsBermudaPki (const u8 *data, size_t size, size_t file_size)
{
	if (file_size && size > file_size)
		size = file_size; // bytes past the end of the file are not part of it
	if (!data || size < BT_PKI_HEADER_SIZE || memcmp (data, BT_PKI_MAGIC, BT_PKI_MAGIC_LEN))
		return 0;
	if (!file_size)
		file_size = size;
	const u32 count = rd_le32 (data + BT_PKI_MAGIC_LEN);
	if (!count || count > 0x100000)
		return 0;
	u64 pos = BT_PKI_HEADER_SIZE, min_offset = UINT64_MAX;
	u32 seen = 0;
	for (; seen < count; seen++)
	{
		if (pos + 4 > size)
			break;
		const u32 name_len = rd_le32 (data + pos);
		if (!name_len || name_len > 1024)
			return 0;
		if (pos + 4 + name_len + 8 > size)
			break;
		for (u32 i = 0; i < name_len; i++)
			if (!isprint (data[pos + 4 + i]))
				return 0;
		pos += 4 + name_len;
		const u32 entry_size = rd_le32 (data + pos), entry_offset = rd_le32 (data + pos + 4);
		pos += 8;
		if ((u64)entry_offset + entry_size > file_size)
			return 0;
		if (entry_offset < min_offset)
			min_offset = entry_offset;
	}
	if (!seen)
		return 0;
	return seen < count || min_offset >= pos;
}

enumError DecodeBermudaPki_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsBermudaPki (data, size, file_size))
		return EINVAL;

	u32 entry_count = rd_le32 (data + BT_PKI_MAGIC_LEN);

	fprintf (f, "# Planet-G IMAGE_WII_COMPACT texture pack (.PKI)\n");
	fprintf (f, "entry_count = %u\n\n", entry_count);

	size_t pos = BT_PKI_HEADER_SIZE;
	for (u32 i = 0; i < entry_count; i++)
	{
		if (pos + 4 > size)
		{
			fprintf (f, "# entry table truncated at entry %u -- stopping\n", i);
			break;
		}
		u32 name_len = rd_le32 (data + pos);
		pos += 4;
		if ((u64)pos + name_len + 8 > size)
		{
			fprintf (f, "# entry %u name/trailer out-of-bounds -- stopping\n", i);
			break;
		}
		const u8 *name = data + pos;
		pos += name_len;
		u32 data_size = rd_le32 (data + pos);
		pos += 4;
		u32 data_offset = rd_le32 (data + pos);
		pos += 4;

		fprintf (f, "entry[%u] name=\"", i);
		print_escaped (f, name, name_len);
		fprintf (f, "\" size=%u offset=0x%x\n", data_size, data_offset);
	}

	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (3) ".pgf" font resource

enum
{
	BT_PGF_MIN_HEADER = 8
};

// Family name, point size, padding, then the four glyph tables and the atlas size must all be buffered:
// glyph rectangles have to lie inside the atlas (with a small margin, one real glyph overhangs by a pixel)
// and most of the 256 glyphs have to be present.
int IsBermudaPgf (const u8 *data, size_t size, size_t file_size)
{
	if (file_size && size > file_size)
		size = file_size; // bytes past the end of the file are not part of it
	if (!data || size < BT_PGF_MIN_HEADER)
		return 0;
	const u32 name_len = rd_le32 (data);
	if (!name_len || name_len > 64)
		return 0;
	const u64 tables = 4 + (u64)name_len + 4 + 16, end = tables + 1024 * 4 + 8;
	if (end > size || (file_size && end > file_size))
		return 0;
	for (u32 i = 0; i < name_len; i++)
		if (!isprint (data[4 + i]))
			return 0;
	const u32 point_size = rd_le32 (data + 4 + name_len);
	if (point_size < 4 || point_size > 256)
		return 0;
	for (int i = 0; i < 4; i++)
		if (rd_le32 (data + 8 + name_len + 4 * i) > 64)
			return 0;
	const u32 atlas_w = rd_le32 (data + end - 8), atlas_h = rd_le32 (data + end - 4);
	if (!atlas_w || atlas_w > 8192 || !atlas_h || atlas_h > 8192)
		return 0;
	uint glyphs = 0;
	for (uint i = 0; i < 256; i++)
	{
		const u32 x = rd_le32 (data + tables + 4 * i), y = rd_le32 (data + tables + 1024 + 4 * i);
		const u32 w = rd_le32 (data + tables + 2048 + 4 * i), h = rd_le32 (data + tables + 3072 + 4 * i);
		if (!w || !h)
			continue;
		if (w > 512 || h > 512 || (u64)x + w > (u64)atlas_w + 16 || (u64)y + h > (u64)atlas_h + 16)
			return 0;
		glyphs++;
	}
	return glyphs >= 64;
}

// The run-length tail after the atlas size: one token per run, a byte b != 0 is a run of b pixels and a 0 byte
// is followed by a u32 run length. Some files put one extra 0 byte before the first token; the start that
// makes the runs cover exactly atlas_width * atlas_height pixels is the right one. Each run is one colour of
// the font's .MWT texture and every run boundary is a colour change, but the colours themselves are not
// stored: for a two-colour atlas the runs alternate background / ink, starting with background.
static bool pgf_tail (const u8 *data, size_t size, size_t *start, u32 *atlas_w, u32 *atlas_h)
{
	const u32 name_len = rd_le32 (data);
	const size_t end = 4 + (size_t)name_len + 4 + 16 + 1024 * 4 + 8;
	*atlas_w = rd_le32 (data + end - 8);
	*atlas_h = rd_le32 (data + end - 4);
	const u64 area = (u64)*atlas_w * *atlas_h;
	for (size_t s = end; s <= end + 1 && s < size; s++)
	{
		if (s > end && data[end])
			break;
		u64 sum = 0;
		size_t p = s;
		while (p < size && sum <= area)
		{
			const u8 b = data[p++];
			if (b)
				sum += b;
			else
			{
				if (p + 4 > size)
					break;
				sum += rd_le32 (data + p);
				p += 4;
			}
		}
		if (p == size && sum == area)
		{
			*start = s;
			return true;
		}
	}
	return false;
}

// Returns 0 (no valid run tail), 1 (two-colour atlas) or 2 (more colours). A two-colour atlas puts every ink
// pixel inside a glyph rectangle grown by one pixel; checked on all 21 sample fonts (the 9 two-colour ones
// have no ink outside, the multi-colour ones 7.8% to 49% under the alternating reading). With RGBA != NULL
// a two-colour atlas is written as white ink on transparent background.
static int pgf_atlas (const u8 *data, size_t size, size_t file_size, u8 *rgba)
{
	if (file_size && size > file_size)
		size = file_size;
	if (!IsBermudaPgf (data, size, file_size) || (file_size && size < file_size))
		return 0;
	size_t p;
	u32 aw, ah;
	if (!pgf_tail (data, size, &p, &aw, &ah))
		return 0;

	u8 *inside = CALLOC (1, (size_t)aw * ah);
	const size_t tables = 4 + (size_t)rd_le32 (data) + 4 + 16;
	for (uint i = 0; i < 256; i++)
	{
		const u32 x = rd_le32 (data + tables + 4 * i), y = rd_le32 (data + tables + 1024 + 4 * i);
		const u32 w = rd_le32 (data + tables + 2048 + 4 * i), h = rd_le32 (data + tables + 3072 + 4 * i);
		if (!w || !h)
			continue;
		const u32 x0 = x ? x - 1 : 0, y0 = y ? y - 1 : 0;
		const u32 x1 = x + w + 1 < aw ? x + w + 1 : aw, y1 = y + h + 1 < ah ? y + h + 1 : ah;
		for (u32 yy = y0; yy < y1 && x0 < x1; yy++)
			memset (inside + (size_t)yy * aw + x0, 1, x1 - x0);
	}

	int kind = 1;
	size_t pix = 0;
	for (uint run = 0; p < size; run++)
	{
		const u8 b = data[p++];
		u32 len = b;
		if (!b)
		{
			len = rd_le32 (data + p);
			p += 4;
		}
		if (run & 1)
			for (size_t k = pix; k < pix + len; k++)
			{
				if (!inside[k])
					kind = 2;
				if (rgba)
					rgba[4 * k + 3] = 0xff;
			}
		pix += len;
	}
	FREE (inside);
	return kind;
}

int GetBermudaPgfAtlasKind (const u8 *data, size_t size, size_t file_size)
{
	return data ? pgf_atlas (data, size, file_size, 0) : 0;
}

enumError DecodeBermudaPgf_RGBA (u8 **rgba, uint *width, uint *height, const u8 *data, size_t size,
	size_t file_size)
{
	if (!rgba || !width || !height || !data || pgf_atlas (data, size, file_size, 0) != 1)
		return ERR_INVALID_DATA;
	size_t p;
	u32 aw, ah;
	pgf_tail (data, file_size && size > file_size ? file_size : size, &p, &aw, &ah);
	u8 *out = MALLOC ((size_t)aw * ah * 4);
	for (size_t k = 0; k < (size_t)aw * ah; k++)
	{
		out[4 * k] = out[4 * k + 1] = out[4 * k + 2] = 0xff;
		out[4 * k + 3] = 0;
	}
	pgf_atlas (data, size, file_size, out);
	*rgba = out;
	*width = aw;
	*height = ah;
	return ERR_OK;
}

enumError DecodeBermudaPgf_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsBermudaPgf (data, size, file_size))
		return EINVAL;

	u32 name_len = rd_le32 (data);
	const u8 *name = data + 4;
	u32 pos = 4 + name_len;
	u32 point_size = rd_le32 (data + pos);
	pos += 4;

	fprintf (f, "# Planet-G bitmap font metrics (.pgf)\n");
	fprintf (f, "family = \"");
	print_escaped (f, name, name_len);
	fprintf (f, "\"\n");
	fprintf (f, "point_size = %u\n", point_size);

	if (pos + 16 <= size)
	{
		u32 pad_left = rd_le32 (data + pos);
		u32 pad_top = rd_le32 (data + pos + 4);
		u32 pad_right = rd_le32 (data + pos + 8);
		u32 pad_bottom = rd_le32 (data + pos + 12);
		pos += 16;
		fprintf (f, "padding = [%u, %u, %u, %u]\n", pad_left, pad_top, pad_right, pad_bottom);
	}

	if (pos + 1024 * 4 + 8 <= size)
	{
		const u8 *gx = data + pos;
		const u8 *gy = data + pos + 256 * 4;
		const u8 *gw = data + pos + 512 * 4;
		const u8 *gh = data + pos + 768 * 4;
		pos += 1024 * 4;

		u32 atlas_w = rd_le32 (data + pos);
		u32 atlas_h = rd_le32 (data + pos + 4);
		pos += 8;

		fprintf (f, "atlas_width = %u\n", atlas_w);
		fprintf (f, "atlas_height = %u\n", atlas_h);
		fprintf (f, "glyph_count = 256\n");

		for (uint i = 0; i < 256; i++)
		{
			u32 x = rd_le32 (gx + i * 4);
			u32 y = rd_le32 (gy + i * 4);
			u32 w = rd_le32 (gw + i * 4);
			u32 h = rd_le32 (gh + i * 4);
			if (w > 0 && h > 0)
			{
				if (i >= 32 && i <= 126)
					fprintf (f, "glyph[0x%02x '%c'] = x:%u y:%u w:%u h:%u\n", i, (char)i, x, y, w, h);
				else
					fprintf (f, "glyph[0x%02x] = x:%u y:%u w:%u h:%u\n", i, x, y, w, h);
			}
		}
	}

	switch (pgf_atlas (data, size, file_size, 0))
	{
	case 1:
		fprintf (f, "# atlas: two-colour run-length bitmap, decoded to <file>.png (white ink on transparent)\n");
		break;
	case 2:
		fprintf (f, "# atlas: run-length map of colour changes in the companion .MWT texture; the colours are\n"
					"# not stored, so the image comes from that .MWT (which converts to PNG)\n");
		break;
	}
	return ERR_OK;
}
