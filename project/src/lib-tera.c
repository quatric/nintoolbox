// SPDX-License-Identifier: GPL-2.0+
// Nintendo "tera" terrain tiles (TARB) and scenes (TSCB) of Mario Kart World. See lib-tera.h.

#include "lib-tera.h"
#include "lib-bntx.h"
#include "lib-sbuf.h"
#include "lib-zstd.h"
#include <math.h>
#include <png.h>
#include <setjmp.h>

static u16 t_le16 (const u8 *p) { return (u16)(p[0] | p[1] << 8); }
static u32 t_le32 (const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static s32 t_les32 (const u8 *p) { return (s32)t_le32 (p); }

static float t_lef32 (const u8 *p)
{
	const u32 v = t_le32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

bool IsTarbFile (const u8 *data, size_t size)
{
	return data && size >= 0x14 && !memcmp (data, "TARB", 4) && t_le16 (data + 0x0a) >= 0x14
		&& t_le16 (data + 0x0a) <= size;
}

bool IsTscbFile (const u8 *data, size_t size)
{
	return data && size >= 0x80 && (!memcmp (data, "TSCB", 4) || !memcmp (data, "TSCZ", 4));
}

void ResetTeraImage (TeraImage_t *img)
{
	if (img)
	{
		FREE (img->main);
		FREE (img->extra);
		memset (img, 0, sizeof (*img));
	}
}

//-----------------------------------------------------------------------------
// PNG / Radiance HDR encoders

static void png_mem_write (png_structp png, png_bytep data, png_size_t len)
{
	sb_write ((sbuf_t *)png_get_io_ptr (png), data, len);
}

static void png_mem_flush (png_structp png)
{
	(void)png;
}

enumError EncodePngMem (const u8 *pixels, uint width, uint height, uint channels, uint depth,
	u8 **out, size_t *out_size)
{
	*out = 0;
	*out_size = 0;
	if (!pixels || !width || !height || channels < 1 || channels > 4 || (depth != 8 && depth != 16))
		return ERR_INVALID_DATA;

	sbuf_t *sb = CALLOC (1, sizeof (*sb));
	png_structp png = png_create_write_struct (PNG_LIBPNG_VER_STRING, 0, 0, 0);
	png_infop info = png ? png_create_info_struct (png) : 0;
	if (!sb || !png || !info)
	{
		png_destroy_write_struct (&png, &info);
		FREE (sb);
		return ERR_OUT_OF_MEMORY;
	}
	if (setjmp (png_jmpbuf (png)))
	{
		png_destroy_write_struct (&png, &info);
		FREE (sb->d);
		FREE (sb);
		return ERR_INVALID_DATA;
	}
	static const int ctype[] = { PNG_COLOR_TYPE_GRAY, PNG_COLOR_TYPE_GRAY_ALPHA, PNG_COLOR_TYPE_RGB,
		PNG_COLOR_TYPE_RGB_ALPHA };
	png_set_write_fn (png, sb, png_mem_write, png_mem_flush);
	png_set_IHDR (png, info, width, height, (int)depth, ctype[channels - 1], PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_write_info (png, info);
	if (depth == 16)
		png_set_swap (png); // samples are given little endian
	const size_t stride = (size_t)width * channels * (depth / 8);
	for (uint y = 0; y < height; y++)
		png_write_row (png, (png_bytep)(pixels + y * stride));
	png_write_end (png, info);
	png_destroy_write_struct (&png, &info);

	char *text = 0;
	size_t len = 0;
	const enumError err = sb_finish (sb, &text, &len);
	FREE (sb);
	*out = (u8 *)text;
	*out_size = len;
	return err;
}

// Radiance RGBE (uncompressed scanlines) of WIDTH*HEIGHT float RGB triplets.
static enumError encode_hdr_mem (const float *rgb, uint width, uint height, u8 **out, size_t *out_size)
{
	sbuf_t b = { 0 };
	sb_printf (&b, "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y %u +X %u\n", height, width);
	if (!sb_reserve (&b, (size_t)width * height * 4))
		return sb_finish (&b, (char **)out, out_size);
	for (size_t i = 0; i < (size_t)width * height; i++)
	{
		const float r = rgb[3 * i], g = rgb[3 * i + 1], bl = rgb[3 * i + 2];
		float v = r > g ? r : g;
		if (bl > v)
			v = bl;
		u8 e[4] = { 0, 0, 0, 0 };
		if (v >= 1e-32f)
		{
			int ex;
			const float m = frexpf (v, &ex) * 256.0f / v;
			e[0] = (u8)(r > 0 ? r * m : 0);
			e[1] = (u8)(g > 0 ? g * m : 0);
			e[2] = (u8)(bl > 0 ? bl * m : 0);
			e[3] = (u8)(ex + 128);
		}
		sb_write (&b, e, 4);
	}
	return sb_finish (&b, (char **)out, out_size);
}

//-----------------------------------------------------------------------------
// TARB pixels

// The delta packed ("rearranged") form of the data block: for every channel a plane of
// grid*grid values. The low byte of each value is split into nibbles: the first half-plane holds
// {even.lo, odd.lo}, the second {even.hi, odd.hi} for every pixel pair of a row. Wider channels
// keep their higher bytes in further plain planes. Each value is the difference to the previous
// pixel of the same row plus a bias (8 for 8 bit channels, 0x78 otherwise).
static void tera_unpack_delta (const u8 *src, u8 *dst, uint grid, uint channels, uint bpc)
{
	const size_t plane = (size_t)grid * grid, half = plane / 2;
	const u32 bias = bpc == 1 ? 8 : 0x78, mask = bpc >= 4 ? 0xffffffffu : (1u << (8 * bpc)) - 1;
	for (uint c = 0; c < channels; c++)
	{
		const u8 *pl = src + c * plane * bpc;
		for (uint y = 0; y < grid; y++)
		{
			u32 acc = 0;
			for (uint x = 0; x + 1 < grid + 1; x += 2)
			{
				const size_t i = (size_t)y * grid + x;
				const u8 lo = pl[i >> 1], hi = pl[half + (i >> 1)];
				u32 even = (hi & 0xf0) | lo >> 4, odd = (hi & 0x0f) << 4 | (lo & 0x0f);
				for (uint k = 1; k < bpc; k++)
				{
					even |= (u32)pl[k * plane + i] << (8 * k);
					if (x + 1 < grid)
						odd |= (u32)pl[k * plane + i + 1] << (8 * k);
				}
				for (uint s = 0; s < 2 && x + s < grid; s++)
				{
					acc += (s ? odd : even) - bias;
					const u32 v = acc & mask;
					u8 *d = dst + ((i + s) * channels + c) * bpc;
					for (uint k = 0; k < bpc; k++)
						d[k] = (u8)(v >> (8 * k));
				}
			}
		}
	}
}

static float tera_f11 (uint v, uint mbits)
{
	const uint e = v >> mbits, m = v & ((1u << mbits) - 1);
	if (!e)
		return ldexpf ((float)m / (float)(1u << mbits), -14);
	if (e == 31)
		return m ? 0.0f : 65504.0f;
	return ldexpf (1.0f + (float)m / (float)(1u << mbits), (int)e - 15);
}

// Pixel layout of a channel-type quad (bytes 0x04 or 0x14 of the info block).
typedef struct tera_fmt_t
{
	uint channels, bpc; // bytes per channel (1/2); 0 for packed formats
	bool r11g11b10;
	uint bc; // 0, or the BC variant (1 = BC1, 3 = BC3)
	uint bpt; // bytes per texel (or per 4x4 block for BC)
} tera_fmt_t;

static bool tera_parse_fmt (const u8 *q, tera_fmt_t *f)
{
	memset (f, 0, sizeof (*f));
	if (q[0] & 0x80)
	{
		switch (q[3])
		{
		case 0x30: f->bc = 1, f->bpt = 8, f->channels = 4; return true;
		case 0x34: f->bc = 3, f->bpt = 16, f->channels = 4; return true;
		}
		return false;
	}
	if (q[0] == 8 && q[1] == 8 && q[2] == 7 && !q[3])
	{
		f->r11g11b10 = true, f->channels = 3, f->bpt = 4;
		return true;
	}
	uint n = 0;
	while (n < 4 && q[n])
		n++;
	if (!n)
		return false;
	for (uint i = n; i < 4; i++)
		if (q[i])
			return false;
	for (uint i = 0; i < n; i++)
	{
		const uint t = q[i] & 0x0f, bpc = t == 6 ? 1 : t == 9 ? 2 : 0;
		if (!bpc || (f->bpc && f->bpc != bpc))
			return false;
		f->bpc = bpc;
	}
	f->channels = n;
	f->bpt = n * f->bpc;
	return true;
}

static const char *tera_fmt_name (const tera_fmt_t *f)
{
	static const char *names8[] = { "R8", "RG8", "RGB8", "RGBA8" }, *names16[] = { "R16", "RG16", "RGB16", "RGBA16" };
	return f->bc == 1 ? "BC1" : f->bc == 3 ? "BC3" : f->r11g11b10 ? "R11G11B10F" : f->bpc == 2 ? names16[f->channels - 1] : names8[f->channels - 1];
}

// Turns WIDTH*HEIGHT pixels of format F into the output image(s).
static enumError tera_export (const u8 *px, uint w, uint h, const tera_fmt_t *f, bool normal_map, TeraImage_t *img)
{
	const size_t n = (size_t)w * h;
	img->width = w, img->height = h;
	if (f->r11g11b10)
	{
		float *rgb = MALLOC (n * 3 * sizeof (float));
		if (!rgb)
			return ERR_OUT_OF_MEMORY;
		for (size_t i = 0; i < n; i++)
		{
			const u32 v = t_le32 (px + 4 * i);
			rgb[3 * i] = tera_f11 (v & 0x7ff, 6);
			rgb[3 * i + 1] = tera_f11 (v >> 11 & 0x7ff, 6);
			rgb[3 * i + 2] = tera_f11 (v >> 22 & 0x3ff, 5);
		}
		img->hdr = true;
		const enumError err = encode_hdr_mem (rgb, w, h, &img->main, &img->main_size);
		FREE (rgb);
		return err;
	}

	const uint ch = f->bc ? 4 : f->channels, bpc = f->bc ? 1 : f->bpc;
	if (ch == 1 || ch == 3)
		return EncodePngMem (px, w, h, ch, 8 * bpc, &img->main, &img->main_size);

	// 2 channels: RG plus a derived (normal map) or empty blue channel; 4 channels: RGB plus a
	// separate grayscale image of the 4th channel (terrain layers never use it as opacity).
	u8 *rgb = MALLOC (n * 3 * bpc);
	if (!rgb)
		return ERR_OUT_OF_MEMORY;
	// The 4th channel gets its own image unless it is constant 0 or constant full scale.
	const uint a0 = bpc == 1 ? px[3] : t_le16 (px + 6), amax = bpc == 1 ? 0xff : 0xffff;
	bool has_extra = ch == 4 && a0 && a0 != amax;
	for (size_t i = 0; i < n; i++)
	{
		const u8 *s = px + i * ch * bpc;
		u8 *d = rgb + i * 3 * bpc;
		memcpy (d, s, 2 * bpc);
		if (ch == 4)
		{
			memcpy (d + 2 * bpc, s + 2 * bpc, bpc);
			if (memcmp (s + 3 * bpc, px + 3 * bpc, bpc))
				has_extra = true;
		}
		else if (normal_map && bpc == 1)
		{
			const float x = s[0] / 127.5f - 1.0f, y = s[1] / 127.5f - 1.0f, zz = 1.0f - x * x - y * y;
			d[2] = (u8)lrintf ((zz > 0 ? sqrtf (zz) : 0) * 127.5f + 127.5f);
		}
		else
			memset (d + 2 * bpc, 0, bpc);
	}
	enumError err = EncodePngMem (rgb, w, h, 3, 8 * bpc, &img->main, &img->main_size);
	if (!err && has_extra)
	{
		for (size_t i = 0; i < n; i++)
			memcpy (rgb + i * bpc, px + (i * 4 + 3) * bpc, bpc);
		err = EncodePngMem (rgb, w, h, 1, 8 * bpc, &img->extra, &img->extra_size);
	}
	FREE (rgb);
	return err;
}

enumError DecodeTarb (const u8 *data, size_t size, bool normal_map, TeraImage_t *img)
{
	memset (img, 0, sizeof (*img));
	if (!IsTarbFile (data, size))
		return ERR_INVALID_DATA;

	const uint hs = t_le16 (data + 0x0a);
	const u32 csize = t_le32 (data + 0x10);
	u8 *alloc = 0;
	const u8 *pl;
	size_t plen;
	if (csize)
	{
		if (csize > size - hs)
			return ERR_INVALID_DATA;
		uint n = 0;
		if (DecodeZSTD (&alloc, &n, data + hs, csize) || !alloc)
			return ERR_INVALID_DATA;
		pl = alloc, plen = n;
	}
	else
		pl = data + hs, plen = size - hs;

	enumError err = ERR_INVALID_DATA;
	u8 *px = 0;
	if (plen < 0x34)
		goto done;

	const uint grid = pl[0] << 8 | pl[1], margin = pl[2];
	const bool packed = (pl[8] & 0x80) != 0;
	if (!grid || grid > 0x4000 || 2 * margin >= grid)
		goto done;

	const u8 *blk[3] = { 0, 0, 0 };
	size_t blk_size[3] = { 0, 0, 0 };
	for (uint i = 0; i < 3; i++)
	{
		const uint field = 0x1c + 8 * i;
		const s32 off = t_les32 (pl + field);
		const u32 sz = t_le32 (pl + field + 4);
		if (!off || !sz)
			continue;
		const s64 start = (s64)field + off;
		if (start < 0x34 || (u64)start + sz > plen)
			goto done;
		blk[i] = pl + start, blk_size[i] = sz;
	}

	tera_fmt_t df, gf;
	const bool have_df = tera_parse_fmt (pl + 4, &df), have_gf = tera_parse_fmt (pl + 0x14, &gf);
	const size_t texels = (size_t)grid * grid;
	if (blk[0] && have_df && !df.bc && blk_size[0] >= texels * df.bpt)
	{
		// The data block: plain pixels, or delta packed ones.
		px = MALLOC (texels * df.bpt);
		if (!px)
		{
			err = ERR_OUT_OF_MEMORY;
			goto done;
		}
		if (packed && df.bpc)
			tera_unpack_delta (blk[0], px, grid, df.channels, df.bpc);
		else
			memcpy (px, blk[0], texels * df.bpt);
		err = tera_export (px, grid, grid, &df, normal_map, img);
		snprintf (img->desc, sizeof (img->desc), "%ux%u %s%s", grid, grid, tera_fmt_name (&df), packed ? ", delta packed" : "");
	}
	else if (blk[2] && have_gf)
	{
		// The GPU block: mip 0, block linear (Orin GOBs, block height 1).
		const uint bw = gf.bc ? 4 : 1;
		u8 *lin = 0;
		uint lin_size = 0;
		if (BntxDeswizzleEx (&lin, &lin_size, blk[2], (uint)blk_size[2], grid, grid, bw, bw, gf.bpt, 0, 0, false, true) || !lin)
			goto done;
		if (gf.bc)
		{
			const uint nbx = (grid + 3) / 4;
			px = MALLOC (texels * 4);
			if (!px)
			{
				FREE (lin);
				err = ERR_OUT_OF_MEMORY;
				goto done;
			}
			for (uint by = 0; by < nbx; by++)
				for (uint bx = 0; bx < nbx; bx++)
				{
					u8 tile[64];
					const u8 *b = lin + ((size_t)by * nbx + bx) * gf.bpt;
					if (gf.bc == 1)
						decode_bc1_block (b, tile, true);
					else
						decode_bc3_block (b, tile);
					for (uint y = 0; y < 4 && by * 4 + y < grid; y++)
						for (uint x = 0; x < 4 && bx * 4 + x < grid; x++)
							memcpy (px + (((size_t)(by * 4 + y)) * grid + bx * 4 + x) * 4, tile + (y * 4 + x) * 4, 4);
				}
			FREE (lin);
		}
		else
			px = lin;
		err = tera_export (px, grid, grid, &gf, normal_map, img);
		const uint mips = pl[0x19];
		snprintf (img->desc, sizeof (img->desc), "%ux%u %s%s", grid, grid, tera_fmt_name (&gf), mips > 1 ? " (+mips)" : "");
	}

done:
	FREE (px);
	FREE (alloc);
	if (err)
		ResetTeraImage (img);
	return err;
}

//-----------------------------------------------------------------------------
// TSCB scene

static const char *tscb_text (const u8 *d, size_t size, size_t field)
{
	if (field + 4 > size)
		return "";
	const s32 off = t_les32 (d + field);
	const s64 at = (s64)field + off;
	if (!off || at < 0 || (u64)at >= size || !memchr (d + at, 0, size - at))
		return "";
	return (const char *)d + at;
}

// Entry I of a table of self-relative s32 offsets at TABLE (or 0 when out of range).
static size_t tscb_entry (const u8 *d, size_t size, size_t table, uint i, size_t need)
{
	const size_t field = table + 4 * (size_t)i;
	if (field + 4 > size)
		return 0;
	const s64 at = (s64)field + t_les32 (d + field);
	return at > 0 && (u64)at + need <= size ? (size_t)at : 0;
}

static const char *tscb_format_name (uint code)
{
	switch (code)
	{
	case 0x01: return "R8";
	case 0x05: return "R16";
	case 0x0a: return "RG8";
	case 0x1a: return "R11G11B10F";
	case 0x1d: return "RGBA8";
	case 0x27: return "RGBA16";
	case 0x30: return "BC1";
	case 0x34: return "BC3";
	}
	return 0;
}

static void sb_float (sbuf_t *b, float f)
{
	if (f == (float)(s32)f && fabsf (f) < 1e9f)
		sb_printf (b, "%d", (s32)f);
	else
		sb_printf (b, "%.9g", f);
}

enumError ConvertTscbToYaml (const u8 *data, size_t size, char **text, size_t *text_size)
{
	*text = 0;
	*text_size = 0;
	if (!IsTscbFile (data, size))
		return ERR_INVALID_DATA;

	u8 *alloc = 0;
	const u8 *d = data;
	if (!memcmp (data, "TSCZ", 4))
	{
		uint n = 0;
		if (DecodeZSTD (&alloc, &n, data + 0x10, (uint)(size - 0x10)) || !alloc || n < 0x80 || memcmp (alloc, "TSCB", 4))
		{
			FREE (alloc);
			return ERR_INVALID_DATA;
		}
		d = alloc, size = n;
	}

	sbuf_t b = { 0 };
	sb_printf (&b, "# Nintendo terrain scene (TSCB)\nversion: 0x%04x\nworld_scale: ", t_le32 (d + 4));
	sb_float (&b, t_lef32 (d + 0x10));
	sb_puts (&b, "\nheight_scale: ");
	sb_float (&b, t_lef32 (d + 0x14));
	sb_puts (&b, "\n");
	for (uint o = 0x18; o < 0x70; o += 4)
		if (o < 0x30 || o > 0x38)
		{
			const u32 v = t_le32 (d + o);
			if (!v)
				continue;
			sb_printf (&b, "unknown_0x%02x: ", o);
			if (v < 0x10000)
				sb_printf (&b, "%u\n", v);
			else
			{
				sb_float (&b, t_lef32 (d + o));
				sb_puts (&b, "\n");
			}
		}

	// Layer table: id byte, size, margin, mips, format code, default value, names.
	const uint n_info = t_le32 (d + 0x30), n_mat = t_le32 (d + 0x34), n_area = t_le32 (d + 0x38);
	const size_t info_tab = t_le32 (d + 0x70) ? 0x70 + (size_t)t_les32 (d + 0x70) : 0;
	const size_t mat_tab = t_le32 (d + 0x74) ? 0x74 + (size_t)t_les32 (d + 0x74) : 0;
	const size_t area_tab = t_le32 (d + 0x78) ? 0x78 + (size_t)t_les32 (d + 0x78) : 0;
	struct
	{
		u8 id;
		const char *ext;
	} layer[64];
	uint n_layer = 0;
	sb_puts (&b, "layers:\n");
	for (uint i = 0; info_tab && i < n_info && i < 1024; i++)
	{
		const size_t e = tscb_entry (d, size, info_tab, i, 0x70);
		if (!e)
			continue;
		const char *name = tscb_text (d, size, e + 0x60), *ext = tscb_text (d, size, e + 0x64);
		const char *cls = tscb_text (d, size, e + 0x6c), *fmt = tscb_format_name (t_le32 (d + e + 0x1c));
		if (n_layer < 64)
			layer[n_layer].id = d[e + 1], layer[n_layer++].ext = ext;
		sb_printf (&b, "  - name: %s\n    extension: %s\n    id: 0x%02x\n    size: %u\n    margin: %u\n    mips: %u\n",
			name, ext, d[e + 1], t_le32 (d + e + 0x10), t_le32 (d + e + 0x14), t_le32 (d + e + 0x18));
		if (fmt)
			sb_printf (&b, "    format: %s\n", fmt);
		else
			sb_printf (&b, "    format: 0x%x\n", t_le32 (d + e + 0x1c));
		sb_puts (&b, "    default: [");
		for (uint k = 0; k < 4; k++)
		{
			if (k)
				sb_puts (&b, ", ");
			sb_float (&b, t_lef32 (d + e + 0x40 + 4 * k));
		}
		sb_printf (&b, "]\n    flags: 0x%x\n", t_le32 (d + e + 0x50));
		if (*cls)
			sb_printf (&b, "    class: %s\n", cls);
	}

	sb_puts (&b, "materials:\n");
	for (uint i = 0; mat_tab && i < n_mat && i < 0x10000; i++)
	{
		const size_t e = tscb_entry (d, size, mat_tab, i, 0x40);
		if (!e)
			continue;
		sb_printf (&b, "  - {index: %u, texture: %u, uv_scale: [", i, t_le32 (d + e));
		sb_float (&b, t_lef32 (d + e + 0x10));
		sb_puts (&b, ", ");
		sb_float (&b, t_lef32 (d + e + 0x14));
		sb_puts (&b, "], params: [");
		sb_float (&b, t_lef32 (d + e + 0x20));
		sb_puts (&b, ", ");
		sb_float (&b, t_lef32 (d + e + 0x24));
		sb_puts (&b, "], color: [");
		for (uint k = 0; k < 4; k++)
		{
			if (k)
				sb_puts (&b, ", ");
			sb_float (&b, t_lef32 (d + e + 0x30 + 4 * k));
		}
		sb_puts (&b, "]}\n");
	}

	sb_puts (&b, "areas:\n");
	for (uint i = 0; area_tab && i < n_area && i < 0x100000; i++)
	{
		const size_t a = tscb_entry (d, size, area_tab, i, 0x68);
		if (!a)
			continue;
		sb_printf (&b, "  - {name: \"%s\", x: ", tscb_text (d, size, a + 0x60));
		sb_float (&b, t_lef32 (d + a));
		sb_puts (&b, ", z: ");
		sb_float (&b, t_lef32 (d + a + 4));
		sb_puts (&b, ", size: ");
		sb_float (&b, t_lef32 (d + a + 8));
		sb_puts (&b, ", files: [");
		const uint n_file = t_le32 (d + a + 0x20);
		const size_t ftab = t_le32 (d + a + 0x64) ? a + 0x64 + (size_t)t_les32 (d + a + 0x64) : 0;
		for (uint f = 0; ftab && f < n_file && f < 256; f++)
		{
			const size_t fe = tscb_entry (d, size, ftab, f, 0x28);
			if (!fe)
				continue;
			const char *ext = 0;
			for (uint k = 0; k < n_layer && !ext; k++)
				if (layer[k].id == d[fe + 1])
					ext = layer[k].ext;
			sb_puts (&b, f ? ", " : "");
			if (ext)
				sb_printf (&b, "{ext: %s", ext);
			else
				sb_printf (&b, "{id: 0x%02x", d[fe + 1]);
			const float lo = t_lef32 (d + fe + 0x20), hi = t_lef32 (d + fe + 0x24);
			if (lo || hi)
			{
				sb_puts (&b, ", min: ");
				sb_float (&b, lo);
				sb_puts (&b, ", max: ");
				sb_float (&b, hi);
			}
			sb_puts (&b, "}");
		}
		sb_puts (&b, "]}\n");
	}
	FREE (alloc);
	return sb_finish (&b, text, text_size);
}
