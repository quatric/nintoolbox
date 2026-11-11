// SPDX-License-Identifier: GPL-2.0+
// NintendoWare complex font description (.bfcpx, "FCPX"). See lib-nxfont.h.

#include "lib-nxfont.h"
#include "lib-sbuf.h"

static u32 f_le32 (const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }

static float f_lef32 (const u8 *p)
{
	const u32 v = f_le32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

bool IsFCPX (const u8 *data, size_t size)
{
	return data && size >= 0x18 && !memcmp (data, "FCPX", 4) && data[4] == 0xff && data[5] == 0xfe;
}

static void fcpx_indent (sbuf_t *b, uint depth)
{
	for (uint i = 0; i < depth; i++)
		sb_puts (b, "  ");
}

static void fcpx_name (sbuf_t *b, const u8 *d, size_t size, size_t at)
{
	if (at >= size)
	{
		sb_puts (b, "\"\"");
		return;
	}
	const size_t len = strnlen ((const char *)d + at, size - at);
	sb_quote (b, (const char *)d + at, len);
}

static void fcpx_node (sbuf_t *b, const u8 *d, size_t size, size_t node, uint depth)
{
	fcpx_indent (b, depth);
	if (depth > 8 || node + 4 > size)
	{
		sb_puts (b, "- {error: bad node offset}\n");
		return;
	}
	const u32 kind = f_le32 (d + node);
	switch (kind)
	{
	case 0: // bitmap font: name offset (relative to the node)
		sb_puts (b, "- type: bitmap\n");
		if (node + 8 <= size)
		{
			fcpx_indent (b, depth + 1);
			sb_puts (b, "file: ");
			fcpx_name (b, d, size, node + f_le32 (d + node + 4));
			sb_puts (b, "\n");
		}
		break;

	case 1: // scalable font
	{
		sb_puts (b, "- type: scalable\n");
		if (node + 0x48 > size)
			break;
		const u8 *p = d + node;
		fcpx_indent (b, depth + 1);
		sb_puts (b, "file: ");
		fcpx_name (b, d, size, node + f_le32 (p + 0x2c));
		sb_printf (b, "\n");
		fcpx_indent (b, depth + 1);
		sb_printf (b, "size: %g\n", f_lef32 (p + 4));
		fcpx_indent (b, depth + 1);
		sb_printf (b, "alternate_char: 0x%04x\n", f_le32 (p + 0x14));
		fcpx_indent (b, depth + 1);
		sb_printf (b, "scale: [%g, %g]\n", f_lef32 (p + 0x24), f_lef32 (p + 0x34));
		fcpx_indent (b, depth + 1);
		sb_printf (b, "unknown: [0x%x, 0x%x, 0x%x, 0x%08x]\n", f_le32 (p + 8), f_le32 (p + 0x0c), f_le32 (p + 0x10),
			f_le32 (p + 0x30));
		const u32 n_range = f_le32 (p + 0x40), r_off = f_le32 (p + 0x44);
		if (n_range && n_range < 0x10000 && node + r_off + 8ull * n_range <= size)
		{
			fcpx_indent (b, depth + 1);
			sb_puts (b, "code_ranges: [");
			for (u32 i = 0; i < n_range; i++)
			{
				const u8 *r = d + node + r_off + 8 * i;
				sb_printf (b, "%s[0x%04x, 0x%04x]", i ? ", " : "", f_le32 (r), f_le32 (r + 4));
			}
			sb_puts (b, "]\n");
		}
		break;
	}

	case 2: // pair font: two child nodes (offsets relative to the node)
		sb_puts (b, "- type: pair\n");
		fcpx_indent (b, depth + 1);
		sb_puts (b, "fonts:\n");
		for (uint i = 0; i < 2 && node + 8 + 4 * i <= size; i++)
			fcpx_node (b, d, size, node + f_le32 (d + node + 4 + 4 * i), depth + 2);
		break;

	default:
		sb_printf (b, "- {type: %u}\n", kind);
	}
}

enumError ConvertFCPXToYaml (const u8 *d, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	*text_size = 0;
	if (!IsFCPX (d, size))
		return ERR_INVALID_DATA;
	const uint hs = d[6] | d[7] << 8;
	if (hs < 0x14 || hs + 4 > size)
		return ERR_INVALID_DATA;

	sbuf_t b = { 0 };
	sb_printf (&b, "# NintendoWare complex font (FCPX)\nversion: %u.%u.%u.%u\nfonts:\n", d[11], d[10], d[9], d[8]);
	const u32 n = f_le32 (d + 0x10);
	for (u32 i = 0; i < n && i < 256 && 0x14 + 4 * i + 4 <= size; i++)
	{
		const size_t field = 0x14 + 4 * i;
		fcpx_node (&b, d, size, 0x14 + f_le32 (d + field), 1);
	}
	return sb_finish (&b, text, text_size);
}

//-----------------------------------------------------------------------------
// Switch / Switch 2 BFFNT glyph sheets

#include "lib-bntx.h"
#include "bcn-decoder/bcn_wrapper.h"

bool IsNXFont (const u8 *d, size_t size)
{
	return d && size >= 0x14 && !memcmp (d, "FFNT", 4) && d[4] == 0xff && d[5] == 0xfe && d[11] >= 3;
}

// Finds the TGLP section; returns its offset or 0.
static size_t nxfont_tglp (const u8 *d, size_t size)
{
	size_t o = d[6] | d[7] << 8;
	for (uint n = 0; n < 64 && o + 0x20 <= size; n++)
	{
		if (!memcmp (d + o, "TGLP", 4))
			return o;
		const u32 sz = f_le32 (d + o + 4);
		if (sz < 8)
			break;
		o += sz;
	}
	return 0;
}

// Sum of neighbour differences over a sample of rows: lower means the tiling guess fits.
static u64 nxfont_roughness (const u8 *rgba, uint w, uint h)
{
	u64 sum = 0;
	for (uint y = 0; y + 1 < h; y += 3)
		for (uint x = 0; x + 1 < w; x++)
		{
			const u8 *p = rgba + ((size_t)y * w + x) * 4, *r = p + 4, *dn = p + (size_t)w * 4;
			for (uint c = 0; c < 4; c++)
				sum += (u64)abs (p[c] - r[c]) + (u64)abs (p[c] - dn[c]);
		}
	return sum;
}

static u8 *nxfont_decode (const u8 *src, size_t src_size, uint w, uint h, uint fmt, bool orin)
{
	uint blk = 1, bpp;
	switch (fmt)
	{
	case 0: case 14: bpp = 4; break;
	case 5: bpp = 2; break;
	case 8: bpp = 1; break;
	case 9: case 15: case 12: blk = 4, bpp = 8; break;
	case 10: case 16: case 11: case 17: case 13: case 18: case 19: blk = 4, bpp = 16; break;
	default: return 0;
	}
	const uint hb = (h + blk - 1) / blk, gobs = (hb + 7) / 8;
	uint bh_log2 = 0;
	while ((1u << bh_log2) < gobs && bh_log2 < 4)
		bh_log2++;
	u8 *lin = 0;
	uint lin_size = 0;
	if (BntxDeswizzleEx (&lin, &lin_size, src, (uint)src_size, w, h, blk, blk, bpp, 0, bh_log2, false, orin) || !lin)
		return 0;
	u8 *out = MALLOC ((size_t)w * h * 4);
	if (!out)
	{
		FREE (lin);
		return 0;
	}
	if (fmt == 18 || fmt == 19)
	{
		if (szs_decode_bc7 (lin, w, h, out))
			FREE (out), out = 0;
	}
	else if (blk == 1)
		for (size_t i = 0; i < (size_t)w * h; i++)
		{
			u8 *o = out + 4 * i;
			if (bpp == 4)
				memcpy (o, lin + 4 * i, 4);
			else if (bpp == 2)
				o[0] = o[1] = o[2] = lin[2 * i], o[3] = lin[2 * i + 1];
			else
				o[0] = o[1] = o[2] = lin[i], o[3] = 255;
		}
	else
	{
		const uint nbx = (w + 3) / 4, nby = (h + 3) / 4;
		for (uint by = 0; by < nby; by++)
			for (uint bx = 0; bx < nbx; bx++)
			{
				u8 tile[64];
				const u8 *b = lin + ((size_t)by * nbx + bx) * bpp;
				memset (tile, 255, sizeof (tile));
				switch (fmt)
				{
				case 9: case 15: decode_bc1_block (b, tile, true); break;
				case 10: case 16: decode_bc2_block (b, tile); break;
				case 11: case 17: decode_bc3_block (b, tile); break;
				case 12:
					decode_bc4_block (b, tile);
					break;
				case 13: decode_bc5_block (b, tile); break;
				}
				for (uint y = 0; y < 4 && by * 4 + y < h; y++)
					for (uint x = 0; x < 4 && bx * 4 + x < w; x++)
						memcpy (out + (((size_t)(by * 4 + y)) * w + bx * 4 + x) * 4, tile + (y * 4 + x) * 4, 4);
			}
	}
	FREE (lin);
	return out;
}

enumError DecodeNXFontSheet (const u8 *d, size_t size, uint index, u8 **rgba, uint *width, uint *height, uint *n_sheets)
{
	*rgba = 0;
	if (!IsNXFont (d, size))
		return ERR_INVALID_DATA;
	const size_t t = nxfont_tglp (d, size);
	if (!t)
		return ERR_INVALID_DATA;
	const uint sheets = d[t + 0x0a], fmt = d[t + 0x12] | d[t + 0x13] << 8;
	const uint w = d[t + 0x18] | d[t + 0x19] << 8, h = d[t + 0x1a] | d[t + 0x1b] << 8;
	const u32 sheet_size = f_le32 (d + t + 0x0c), off = f_le32 (d + t + 0x1c);
	if (n_sheets)
		*n_sheets = sheets;
	// Switch 2 fonts embed a whole BNTX (one texture per sheet) at the sheet data offset.
	if (off + 0x40 <= size && !memcmp (d + off, "BNTX", 4))
	{
		bntx_t bntx;
		if (ScanBNTX (&bntx, d + off, (uint)(size - off)) || !bntx.n_textures)
			return ERR_INVALID_DATA;
		if (n_sheets && bntx.n_textures > sheets)
			*n_sheets = bntx.n_textures;
		const enumError err = index < bntx.n_textures ? DecodeBNTX_RGBA (rgba, width, height, &bntx, index) : ERR_INVALID_DATA;
		ResetBNTX (&bntx);
		// The font converter stores these sheets bottom-up (glyph row 0 is the last image row).
		if (!err && *rgba)
		{
			const size_t stride = (size_t)*width * 4;
			u8 *row = MALLOC (stride);
			for (uint y = 0; y < *height / 2; y++)
			{
				u8 *top = *rgba + y * stride, *bot = *rgba + (*height - 1 - y) * stride;
				memcpy (row, top, stride);
				memcpy (top, bot, stride);
				memcpy (bot, row, stride);
			}
			FREE (row);
		}
		return err;
	}
	if (index >= sheets || !w || !h || !sheet_size || (u64)off + (u64)sheet_size * (index + 1) > size)
		return ERR_INVALID_DATA;
	const u8 *src = d + off + (size_t)sheet_size * index;
	u8 *a = nxfont_decode (src, sheet_size, w, h, fmt, false), *b = nxfont_decode (src, sheet_size, w, h, fmt, true);
	if (!a && !b)
		return ERR_INVALID_DATA;
	if (a && b && nxfont_roughness (b, w, h) < nxfont_roughness (a, w, h))
		FREE (a), a = 0;
	if (a)
		FREE (b), b = a;
	*rgba = b;
	*width = w;
	*height = h;
	return ERR_OK;
}
