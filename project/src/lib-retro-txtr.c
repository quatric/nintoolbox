// SPDX-License-Identifier: GPL-2.0+
#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-excite.h"
#include "lib-gtx.h"
#include "lib-bntx.h"
#include "lib-image.h"
#include "lib-retro-txtr.h"
#include "astc/astc_wrapper.h"
#include "bcn-decoder/bcn_wrapper.h"
#include <string.h>

//-----------------------------------------------------------------------------
// Old Retro TXTR (Metroid Prime 1-3 / DKCR)
//-----------------------------------------------------------------------------

#define RETRO_TXTR_MAX_DIM 4096
#define RETRO_TXTR_MAX_MIPS 11
#define RETRO_TXTR_MAX_OUTPUT (512u << 20)

static uint retro_base_size (uint format, uint w, uint h)
{
	// Expected bytes of one mip level, mirroring txtrtool's TXTR_CalcMipSz
	// (GX_CalcMipSz). Returns 0 for C14X2: this tree's GX tile codec
	// (DecodeGXTexture_RGBA) has no C14X2 path, so it is rejected upstream
	// rather than given a wrong size here.
	uint bpp, bw, bh;
	switch (format)
	{
		case RETRO_TXTR_I4:
		case RETRO_TXTR_C4:
			bpp = 4;
			bw = 8;
			bh = 8;
			break;
		case RETRO_TXTR_I8:
		case RETRO_TXTR_IA4:
		case RETRO_TXTR_C8:
			bpp = 8;
			bw = 8;
			bh = 4;
			break;
		case RETRO_TXTR_IA8:
		case RETRO_TXTR_RGB565:
		case RETRO_TXTR_RGB5A3:
			bpp = 16;
			bw = 4;
			bh = 4;
			break;
		case RETRO_TXTR_RGBA8:
			bpp = 32;
			bw = 4;
			bh = 4;
			break;
		case RETRO_TXTR_CMPR:
			bpp = 4;
			bw = 8;
			bh = 8;
			break;
		default:
			return 0;
	}
	const uint tw = (w + bw - 1) / bw * bw;
	const uint th = (h + bh - 1) / bh * bh;
	return tw * th * bpp / 8;
}

static uint retro_max_pal (uint format)
{
	switch (format)
	{
		case RETRO_TXTR_C4:
			return 16;
		case RETRO_TXTR_C8:
			return 256;
		case RETRO_TXTR_C14X2:
			return 16384;
		default:
			return 0;
	}
}

enumError ScanRetroTXTR (retro_txtr_info_t *info, const u8 *data, uint size)
{
	if (!info || !data || size < 12)
		return EINVAL;
	memset (info, 0, sizeof (*info));

	const u32 format = rd_be32 (data);
	if (format > RETRO_TXTR_CMPR)
		return EINVAL;
	const uint w = (uint)data[4] << 8 | data[5];
	const uint h = (uint)data[6] << 8 | data[7];
	const u32 mips = rd_be32 (data + 8);
	if (!w || !h || w > RETRO_TXTR_MAX_DIM || h > RETRO_TXTR_MAX_DIM)
		return EINVAL;
	if (!mips || mips > RETRO_TXTR_MAX_MIPS)
		return EINVAL;

	uint off = 12;
	const bool indexed = format == RETRO_TXTR_C4 || format == RETRO_TXTR_C8
		|| format == RETRO_TXTR_C14X2;
	if (indexed)
	{
		if (size < off + 8)
			return EINVAL;
		const u32 pal_fmt = rd_be32 (data + off);
		if (pal_fmt > RETRO_TXTR_PAL_RGB5A3)
			return EINVAL;
		const uint pal_w = (uint)data[off + 4] << 8 | data[off + 5];
		const uint pal_h = (uint)data[off + 6] << 8 | data[off + 7];
		if (!pal_w || !pal_h)
			return EINVAL;
		const u64 pal_count = (u64)pal_w * pal_h;
		if (pal_count > retro_max_pal (format))
			return EINVAL;
		if ((u64)off + 8 + pal_count * 2 > size)
			return EINVAL;
		info->pal_format = pal_fmt;
		info->pal_count = (uint)pal_count;
		info->palette = data + off + 8;
		off += 8 + (uint)pal_count * 2;
	}

	const uint base = retro_base_size (format, w, h);
	if (!base)
		return EINVAL; // C14X2 or otherwise unsupported here
	if ((u64)off + base > size)
		return EINVAL;

	info->format = format;
	info->width = w;
	info->height = h;
	info->mip_count = mips;
	info->indexed = indexed;
	info->pix_data = data + off;
	info->pix_size = size - off;
	info->base_size = base;
	return ERR_OK;
}

bool IsRetroTXTR (const u8 *data, uint size)
{
	// No magic: the header is just BE words. The DSB ("TXTR"-magic) and
	// Tropical ("RFRM") variants are checked before this, so a BE format
	// word plus sane dimensions plus enough bytes for the base level is a
	// real check, not a guess.
	if (!data || size < 12 || !memcmp (data, "TXTR", 4) || !memcmp (data, "RFRM", 4))
		return false;
	retro_txtr_info_t info;
	return ScanRetroTXTR (&info, data, size) == ERR_OK;
}

// Retro format id -> lib-excite GX id used by DecodeGXTexture_RGBA
// (0..6,14 non-indexed + 8,9 indexed). C14X2 has no GX path there.
static bool retro_to_gx (uint retro_fmt, uint *gx_fmt)
{
	switch (retro_fmt)
	{
		case RETRO_TXTR_I4:
			*gx_fmt = 0;
			return true;
		case RETRO_TXTR_I8:
			*gx_fmt = 1;
			return true;
		case RETRO_TXTR_IA4:
			*gx_fmt = 2;
			return true;
		case RETRO_TXTR_IA8:
			*gx_fmt = 3;
			return true;
		case RETRO_TXTR_C4:
			*gx_fmt = 8;
			return true;
		case RETRO_TXTR_C8:
			*gx_fmt = 9;
			return true;
		case RETRO_TXTR_RGB565:
			*gx_fmt = 4;
			return true;
		case RETRO_TXTR_RGB5A3:
			*gx_fmt = 5;
			return true;
		case RETRO_TXTR_RGBA8:
			*gx_fmt = 6;
			return true;
		case RETRO_TXTR_CMPR:
			*gx_fmt = 14;
			return true;
		default:
			return false;
	}
}

enumError DecodeRetroTXTR_RGBA (
	u8 **dest, uint *width, uint *height, const u8 *src, uint src_size)
{
	if (!dest || !width || !height)
		return EINVAL;
	*dest = 0;
	*width = *height = 0;
	retro_txtr_info_t info;
	if (ScanRetroTXTR (&info, src, src_size))
		return EINVAL;
	uint gx_fmt;
	if (!retro_to_gx (info.format, &gx_fmt))
		return EINVAL; // C14X2: no decoder, fail cleanly
	u8 *rgba = 0;
	const enumError err = DecodeGXTexture_RGBA (&rgba, info.width, info.height, gx_fmt,
		info.pix_data, info.pix_size, info.palette, info.pal_count,
		info.indexed ? info.pal_format : 0);
	if (err)
		return err;
	*dest = rgba;
	*width = info.width;
	*height = info.height;
	return ERR_OK;
}

//--- encoder: inverse tile walk of lib-excite.c's gx_decode() ---------------

static inline u8 retro_to_nibble (u8 v)
{
	return (u8)(((uint)v * 15 + 127) / 255);
}

static inline u8 retro_to_grey (const u8 *p)
{
	return (u8)(((uint)p[0] + p[1] + p[2]) / 3);
}

static void retro_gx_encode (uint gx_fmt, uint w, uint h, const u8 *rgba, u8 *out)
{
#define RGETPX(x, y) (rgba + ((size_t)((uint)(y) < h ? (y) : h - 1) * w + ((uint)(x) < w ? (x) : w - 1)) * 4)
	uint p = 0;
	uint bw = 4, bh = 4;
	switch (gx_fmt)
	{
		case 0:
			bw = 8;
			bh = 8;
			break;
		case 1:
		case 2:
			bw = 8;
			bh = 4;
			break;
		default:
			bw = 4;
			bh = 4;
			break;
	}
	if (gx_fmt == 14)
	{
		bw = 8;
		bh = 8;
	}
	for (uint by = 0; by < h; by += bh)
		for (uint bx = 0; bx < w; bx += bw)
		{
			switch (gx_fmt)
			{
				case 0:
					for (uint y = 0; y < 8; y++)
						for (uint x = 0; x < 8; x += 2)
						{
							const u8 hi = retro_to_nibble (retro_to_grey (RGETPX (bx + x, by + y)));
							const u8 lo = retro_to_nibble (retro_to_grey (RGETPX (bx + x + 1, by + y)));
							out[p++] = (u8)(hi << 4 | lo);
						}
					break;
				case 1:
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 8; x++)
							out[p++] = retro_to_grey (RGETPX (bx + x, by + y));
					break;
				case 2:
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 8; x++)
						{
							const u8 *s = RGETPX (bx + x, by + y);
							out[p++] = (u8)(retro_to_nibble (s[3]) << 4 | retro_to_nibble (retro_to_grey (s)));
						}
					break;
				case 3:
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 4; x++)
						{
							const u8 *s = RGETPX (bx + x, by + y);
							out[p] = s[3];
							out[p + 1] = retro_to_grey (s);
							p += 2;
						}
					break;
				case 4:
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 4; x++)
						{
							const u8 *s = RGETPX (bx + x, by + y);
							const u16 v = (u16)((u16)(s[0] >> 3) << 11 | (u16)(s[1] >> 2) << 5 | (s[2] >> 3));
							out[p] = (u8)(v >> 8);
							out[p + 1] = (u8)v;
							p += 2;
						}
					break;
				case 5:
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 4; x++)
						{
							const u8 *s = RGETPX (bx + x, by + y);
							u16 v;
							if (s[3] >= 224)
								v = (u16)(0x8000 | (u16)(s[0] >> 3) << 10 | (u16)(s[1] >> 3) << 5 | (s[2] >> 3));
							else
								v = (u16)((u16)((s[3] * 7 + 127) / 255) << 12 | (u16)(s[0] >> 4) << 8
									| (u16)(s[1] >> 4) << 4 | (s[2] >> 4));
							out[p] = (u8)(v >> 8);
							out[p + 1] = (u8)v;
							p += 2;
						}
					break;
				case 6:
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 4; x++)
						{
							const u8 *s = RGETPX (bx + x, by + y);
							out[p] = s[3];
							out[p + 1] = s[0];
							p += 2;
						}
					for (uint y = 0; y < 4; y++)
						for (uint x = 0; x < 4; x++)
						{
							const u8 *s = RGETPX (bx + x, by + y);
							out[p] = s[1];
							out[p + 1] = s[2];
							p += 2;
						}
					break;
				case 14:
					for (uint sy = 0; sy < 8; sy += 4)
						for (uint sx = 0; sx < 8; sx += 4)
						{
							u8 vector[64];
							for (uint y = 0; y < 4; y++)
								for (uint x = 0; x < 4; x++)
									memcpy (vector + (y * 4 + x) * 4, RGETPX (bx + sx + x, by + sy + y), 4);
							cmpr_info_t cinfo;
							InitializeCmprInfo (&cinfo);
							CMPR_wiimm (vector, &cinfo);
							CMPR_close_info (vector, &cinfo, out + p, false);
							p += 8;
						}
					break;
				default:
					break;
			}
		}
#undef RGETPX
}

enumError EncodeRetroTXTR_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width,
	uint height, uint retro_format)
{
	if (!dest || !dest_size || !rgba || !width || !height)
		return EINVAL;
	*dest = 0;
	*dest_size = 0;
	if (width > RETRO_TXTR_MAX_DIM || height > RETRO_TXTR_MAX_DIM)
		return EINVAL;
	if (retro_format > RETRO_TXTR_CMPR || retro_format == RETRO_TXTR_C4
		|| retro_format == RETRO_TXTR_C8 || retro_format == RETRO_TXTR_C14X2)
		return EINVAL;
	uint gx_fmt;
	if (!retro_to_gx (retro_format, &gx_fmt))
		return EINVAL;

	const uint pix_size = retro_base_size (retro_format, width, height);
	if (!pix_size || pix_size > RETRO_TXTR_MAX_OUTPUT)
		return EINVAL;
	const uint total = 12 + pix_size;
	if (total > RETRO_TXTR_MAX_OUTPUT)
		return EINVAL;
	u8 *out = CALLOC (1, total);
	if (!out)
		return ERR_CANT_CREATE;
	out[0] = (u8)(retro_format >> 24);
	out[1] = (u8)(retro_format >> 16);
	out[2] = (u8)(retro_format >> 8);
	out[3] = (u8)retro_format;
	out[4] = (u8)(width >> 8);
	out[5] = (u8)width;
	out[6] = (u8)(height >> 8);
	out[7] = (u8)height;
	out[8] = 0;
	out[9] = 0;
	out[10] = 0;
	out[11] = 1; // single mip level
	retro_gx_encode (gx_fmt, width, height, rgba, out + 12);
	*dest = out;
	*dest_size = total;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// Tropical Freeze TXTR (Wii U)
//-----------------------------------------------------------------------------

#define TROPICAL_MAX_DIM 16384
#define TROPICAL_MAX_MIPS 14
#define TROPICAL_MAX_BUFFERS 256

// Retro swizzle id (HEAD +0x30) -> GX2 swizzle, from the reference
// txtr_mapper.py `converted` table (Aruki's research via leamsii's tool).
static const uint tropical_swizzle_tab[8] = { 0, 500, 600, 900, 1200, 1400, 1600, 4000 };

// Retro texture-format id -> GX2 surface format value. Only the low 6 bits
// (storage footprint) plus the sRGB/SNORM upper bits matter to the detiler;
// float/signed kinds visualize through the existing GX2 paths.
static bool tropical_to_gx2 (uint retro_id, uint *gx2_fmt)
{
	switch (retro_id)
	{
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
			*gx2_fmt = 0x01; // R8
			return true;
		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07:
			*gx2_fmt = 0x05; // R16
			return true;
		case 0x08:
			*gx2_fmt = 0x06; // R16_FLOAT
			return true;
		case 0x09:
		case 0x0a:
			*gx2_fmt = 0x0d; // R32
			return true;
		case 0x0c:
		case 0x0d:
			*gx2_fmt = 0x1a; // R8G8B8A8
			return true;
		case 0x0e:
			*gx2_fmt = 0x1f; // RGBA16_FLOAT
			return true;
		case 0x0f:
			*gx2_fmt = 0x23; // RGBA32_FLOAT
			return true;
		case 0x10:
		case 0x11:
			*gx2_fmt = 0x05;
			return true;
		case 0x12:
			*gx2_fmt = 0x11; // D24S8
			return true;
		case 0x13:
		case 0x1f:
			*gx2_fmt = 0x0e; // R32_FLOAT
			return true;
		case 0x14:
		case 0x15:
			*gx2_fmt = 0x31; // BC1
			return true;
		case 0x16:
		case 0x17:
			*gx2_fmt = 0x32; // BC2
			return true;
		case 0x18:
		case 0x19:
			*gx2_fmt = 0x33; // BC3
			return true;
		case 0x1a:
		case 0x1b:
			*gx2_fmt = 0x34; // BC4
			return true;
		case 0x1c:
		case 0x1d:
			*gx2_fmt = 0x35; // BC5
			return true;
		case 0x1e:
			*gx2_fmt = 0x16; // R11G11B10_FLOAT
			return true;
		case 0x20:
			*gx2_fmt = 0x10; // RG16_FLOAT
			return true;
		case 0x21:
			*gx2_fmt = 0x07; // R8G8
			return true;
		default:
			return false;
	}
}

enumError ScanTropicalTXTR (tropical_txtr_info_t *info, const u8 *data, uint size)
{
	if (!info || !data || size < 0x20 + 0x18 + 0x18)
		return EINVAL;
	memset (info, 0, sizeof (*info));

	//--- RFRM form descriptor (0x20 bytes) ---
	if (memcmp (data, "RFRM", 4) || memcmp (data + 0x14, "TXTR", 4))
		return EINVAL;
	const u64 form_size = (u64)rd_be32 (data + 4) << 32 | rd_be32 (data + 8);
	if (form_size + 0x20 != size)
		return EINVAL;

	//--- HEAD chunk ---
	uint pos = 0x20;
	if ((u64)pos + 0x18 > size || memcmp (data + pos, "HEAD", 4))
		return EINVAL;
	const u64 head_size = (u64)rd_be32 (data + pos + 4) << 32 | rd_be32 (data + pos + 8);
	if (head_size < 0x24 || (u64)pos + 0x18 + head_size > size)
		return EINVAL;
	const u8 *hb = data + pos + 0x18;
	const uint tex_type = rd_be32 (hb + 0x00);
	const uint tex_format = rd_be32 (hb + 0x04);
	const uint w = rd_be32 (hb + 0x08);
	const uint h = rd_be32 (hb + 0x0c);
	const uint depth = rd_be32 (hb + 0x10);
	const uint tile_mode = rd_be32 (hb + 0x14);
	const uint swizzle_raw = rd_be32 (hb + 0x18);
	const uint mip_count = rd_be32 (hb + 0x1c);
	if (tex_type > 7 || tex_format > 0x21 || !w || !h || w > TROPICAL_MAX_DIM || h > TROPICAL_MAX_DIM
		|| !depth || depth > 2048 || tile_mode > 15 || swizzle_raw > 7 || !mip_count
		|| mip_count > TROPICAL_MAX_MIPS)
		return EINVAL;
	if (0x20 + (u64)mip_count * 4 + 8 > head_size)
		return EINVAL;
	pos += 0x18 + (uint)head_size;

	//--- GPU chunk ("GPU" prefix; reference code matches 3 chars) ---
	if ((u64)pos + 0x18 > size || memcmp (data + pos, "GPU", 3))
		return EINVAL;
	const u64 gpu_size = (u64)rd_be32 (data + pos + 4) << 32 | rd_be32 (data + pos + 8);
	if ((u64)pos + 0x18 + gpu_size > size)
		return EINVAL;
	const uint gpu_start = pos + 0x18;
	pos += 0x18 + (uint)gpu_size;

	//--- META chunk ---
	if ((u64)pos + 0x18 + 0x28 > size || memcmp (data + pos, "META", 4))
		return EINVAL;
	const u8 *mb = data + pos;
	const uint gpu_sect_off = rd_be32 (mb + 0x20);
	const uint base_align = rd_be32 (mb + 0x24);
	const uint gpu_data_start = rd_be32 (mb + 0x28);
	const uint gpu_sect_size = rd_be32 (mb + 0x2c);
	const uint buf_count = rd_be32 (mb + 0x30);
	if (!buf_count || buf_count > TROPICAL_MAX_BUFFERS)
		return EINVAL;
	if ((u64)pos + 0x34 + (u64)buf_count * 12 > size)
		return EINVAL;
	if ((u64)gpu_sect_off + gpu_sect_size > size || (u64)gpu_data_start > size
		|| (u64)gpu_data_start < gpu_sect_off)
		return EINVAL;

	u64 total_decomp = 0, total_comp = 0;
	const u8 *first_comp = 0;
	uint first_comp_size = 0;
	for (uint i = 0; i < buf_count; i++)
	{
		const u8 *b = mb + 0x34 + i * 12;
		const uint decomp_sz = rd_be32 (b);
		const uint comp_sz = rd_be32 (b + 4);
		const uint buf_off = rd_be32 (b + 8);
		if (!decomp_sz || decomp_sz > RETRO_TXTR_MAX_OUTPUT || !comp_sz)
			return EINVAL;
		if ((u64)gpu_data_start + buf_off + comp_sz > (u64)gpu_sect_off + gpu_sect_size)
			return EINVAL;
		if ((u64)gpu_data_start + buf_off + comp_sz > size)
			return EINVAL;
		if (!i)
		{
			first_comp = data + gpu_data_start + buf_off;
			first_comp_size = comp_sz;
		}
		total_decomp += decomp_sz;
		total_comp += comp_sz;
		if (total_decomp > RETRO_TXTR_MAX_OUTPUT)
			return EINVAL;
	}
	(void)gpu_start;

	info->tex_type = tex_type;
	info->tex_format = tex_format;
	info->width = w;
	info->height = h;
	info->depth = depth;
	info->tile_mode = tile_mode;
	info->swizzle_raw = swizzle_raw;
	info->swizzle = tropical_swizzle_tab[swizzle_raw];
	info->mip_count = mip_count;
	// Pitch is not stored in HEAD; the reference mapper derives it as
	// META-base-alignment / 2. That derivation is kept for the sanity
	// check below, but it cannot be trusted blindly (it demonstrably
	// overshoots on small surfaces), so the decoder re-validates it
	// against the GX2 minimum pitch and the available bytes.
	info->pitch = base_align / 2;
	info->alignment = base_align;
	info->decomp_size = (uint)total_decomp;
	info->comp_size = (uint)total_comp;
	info->comp_data = first_comp;
	info->comp_data_size = first_comp_size;
	info->n_buffers = buf_count;
	return ERR_OK;
}

bool IsTropicalTXTR (const u8 *data, uint size)
{
	if (!data || size < 0x20 + 0x18 + 0x18)
		return false;
	if (memcmp (data, "RFRM", 4))
		return false;
	tropical_txtr_info_t info;
	return ScanTropicalTXTR (&info, data, size) == ERR_OK;
}

//--- Retro LZSS (wiki LZSS_Compression page + reference C) -------------------
// Modes 1/2/3 copy 1/2/4-byte groups; mode 0 is stored. Bounds-checked:
// any truncated or back-referencing descriptor fails instead of overrunning.

static enumError tropical_lzss (u8 **dest, uint *dest_size, const u8 *src, uint src_size,
	uint decomp_size)
{
	if (!dest || !dest_size || !src || !decomp_size || decomp_size > RETRO_TXTR_MAX_OUTPUT)
		return EINVAL;
	*dest = 0;
	*dest_size = 0;
	if (src_size < 4)
		return EINVAL;
	const uint mode = src[0];
	if (mode > 3 || src[1] || src[2] || src[3])
		return EINVAL;
	if (mode == 0)
	{
		if ((u64)src_size - 4 < decomp_size)
			return EINVAL;
		u8 *out = MALLOC (decomp_size);
		if (!out)
			return ERR_CANT_CREATE;
		memcpy (out, src + 4, decomp_size);
		*dest = out;
		*dest_size = decomp_size;
		return ERR_OK;
	}
	const uint group_bytes = mode == 1 ? 1 : mode == 2 ? 2 : 4;
	u8 *out = MALLOC (decomp_size);
	if (!out)
		return ERR_CANT_CREATE;
	uint sp = 4, dp = 0, header = 0, left = 0;
	while (dp < decomp_size)
	{
		if (!left)
		{
			if (sp >= src_size)
			{
				FREE (out);
				return EINVAL;
			}
			header = src[sp++];
			left = 8;
		}
		const bool ref = (header & 0x80) != 0;
		header = (header << 1) & 0xff;
		left--;
		if (!ref)
		{
			if ((u64)sp + group_bytes > src_size || (u64)dp + group_bytes > decomp_size)
			{
				FREE (out);
				return EINVAL;
			}
			memcpy (out + dp, src + sp, group_bytes);
			sp += group_bytes;
			dp += group_bytes;
		}
		else
		{
			if ((u64)sp + 2 > src_size)
			{
				FREE (out);
				return EINVAL;
			}
			const uint b0 = src[sp], b1 = src[sp + 1];
			sp += 2;
			uint count, length;
			if (mode == 1)
			{
				count = (b0 >> 4) + 3;
				length = ((uint)(b0 & 0x0f) << 8) | b1;
			}
			else if (mode == 2)
			{
				count = (b0 >> 4) + 2;
				length = (((uint)(b0 & 0x0f) << 8) | b1) << 1;
			}
			else
			{
				count = (b0 >> 4) + 1;
				length = (((uint)(b0 & 0x0f) << 8) | b1) << 2;
			}
			if (!length || length > dp)
			{
				FREE (out);
				return EINVAL;
			}
			const u64 need = (u64)count * group_bytes;
			if ((u64)dp + need > decomp_size)
			{
				FREE (out);
				return EINVAL;
			}
			uint seek = dp - length;
			for (uint c = 0; c < count; c++)
				for (uint k = 0; k < group_bytes; k++)
					out[dp++] = out[seek++];
		}
	}
	*dest = out;
	*dest_size = decomp_size;
	return ERR_OK;
}

enumError DecodeTropicalTXTR_RGBA (
	u8 **dest, uint *width, uint *height, const u8 *src, uint src_size)
{
	if (!dest || !width || !height)
		return EINVAL;
	*dest = 0;
	*width = *height = 0;
	tropical_txtr_info_t info;
	if (ScanTropicalTXTR (&info, src, src_size))
		return EINVAL;
	if (info.tex_type != 1 || info.depth != 1)
		return EINVAL; // 2D only; cubemaps/arrays fail cleanly
	uint gx2_fmt;
	if (!tropical_to_gx2 (info.tex_format, &gx2_fmt))
		return EINVAL;

	// Pick a pitch that can actually be true: the header-derived value
	// (alignment/2) when it satisfies the GX2 minimum pitch for the tile
	// mode and the base level fits the decompressed bytes, else the
	// minimum pitch itself. gtx_detile() zero-fills out-of-range reads,
	// so a wrong-but-in-range pitch degrades to a scrambled image rather
	// than a crash either way.
	uint pitch = info.pitch;
	{
		uint align = 1;
		if (info.tile_mode == 2 || info.tile_mode == 3)
			align = 8;
		else if (info.tile_mode >= 4 && info.tile_mode <= 15)
		{
			align = 32;
			if (info.tile_mode == 5 || info.tile_mode == 9)
				align = 16;
			else if (info.tile_mode == 6 || info.tile_mode == 10)
				align = 8;
		}
		const uint ew = (gx2_fmt & 0x3f) >= 0x31 && (gx2_fmt & 0x3f) <= 0x35
			? (info.width + 3) / 4
			: info.width;
		const uint min_pitch = (ew + align - 1) & ~(align - 1);
		uint bpp = 32;
		switch (gx2_fmt & 0x3f)
		{
			case 0x01:
			case 0x02:
				bpp = 8;
				break;
			case 0x05:
			case 0x06:
			case 0x07:
			case 0x08:
			case 0x0a:
			case 0x0b:
			case 0x0c:
				bpp = 16;
				break;
			case 0x31:
			case 0x34:
				bpp = 64;
				break;
			case 0x32:
			case 0x33:
			case 0x35:
				bpp = 128;
				break;
			default:
				bpp = 32;
				break;
		}
		const u64 eh = (gx2_fmt & 0x3f) >= 0x31 && (gx2_fmt & 0x3f) <= 0x35
			? (info.height + 3) / 4
			: info.height;
		if (pitch < min_pitch || (u64)pitch * eh * bpp / 8 > info.decomp_size)
			pitch = min_pitch;
	}
	// Decompress every buffer and concatenate (single-buffer files, the
	// common case, are just one iteration). A buffer whose mode byte is
	// outside 0..3 falls back to plain zlib, matching the reference
	// lzz_decompress.py behaviour for zlib-wrapped GPU data.
	const u8 *mb = 0;
	{
		// Re-locate the META buffer table (offsets validated by the scan).
		uint pos = 0x20;
		const u64 head_size = (u64)rd_be32 (src + pos + 4) << 32 | rd_be32 (src + pos + 8);
		pos += 0x18 + (uint)head_size;
		const u64 gpu_size = (u64)rd_be32 (src + pos + 4) << 32 | rd_be32 (src + pos + 8);
		pos += 0x18 + (uint)gpu_size;
		mb = src + pos;
	}
	const uint gpu_data_start = rd_be32 (mb + 0x28);
	u8 *raw = MALLOC (info.decomp_size);
	if (!raw)
		return ERR_CANT_CREATE;
	uint wpos = 0;
	enumError err = ERR_OK;
	for (uint i = 0; i < info.n_buffers; i++)
	{
		const u8 *b = mb + 0x34 + i * 12;
		const uint decomp_sz = rd_be32 (b);
		const uint comp_sz = rd_be32 (b + 4);
		const uint buf_off = rd_be32 (b + 8);
		const u8 *cbuf = src + gpu_data_start + buf_off;
		u8 *dec = 0;
		uint dec_sz = 0;
		if (comp_sz >= 4 && cbuf[0] <= 3 && !cbuf[1] && !cbuf[2] && !cbuf[3])
			err = tropical_lzss (&dec, &dec_sz, cbuf, comp_sz, decomp_sz);
		else
		{
			err = DecodeZlibGrow (&dec, &dec_sz, cbuf, comp_sz);
			if (!err && dec_sz != decomp_sz)
			{
				FREE (dec);
				dec = 0;
				err = EINVAL;
			}
		}
		if (err)
		{
			FREE (dec);
			break;
		}
		memcpy (raw + wpos, dec, decomp_sz);
		wpos += decomp_sz;
		FREE (dec);
	}
	if (err)
	{
		FREE (raw);
		return err;
	}

	u8 *rgba = 0;
	uint w = 0, h = 0;
	// dim 1 (2D), aa 0, slice/sample 0. Pitch is the validated value
	// picked above, not the raw header derivation.
	err = DecodeGX2SurfaceSlice_RGBA (&rgba, &w, &h, 1, info.width, info.height, 1, gx2_fmt,
		0, info.tile_mode, pitch, info.swizzle, 0, 0, raw, wpos);
	FREE (raw);
	if (err)
		return err;
	*dest = rgba;
	*width = w;
	*height = h;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// Metroid Prime Remastered TXTR (Switch)
//-----------------------------------------------------------------------------

#define MPR_MAX_DIM 16384
#define MPR_MAX_MIPS 16
#define MPR_MAX_BUFFERS 64
#define MPR_MAX_INFOS 64

static inline uint mpr_div_round_up (uint n, uint d)
{
	return d ? (n + d - 1) / d : 0;
}

// Same RFRM-form / chunk record shape as lib-mpr-pak.c's read_form() /
// read_chunk(), just little-endian throughout; duplicated here (both are
// tiny and static) rather than shared across translation units.
static bool mpr_read_form (const u8 *data, uint size, uint off, char id[4], u32 *rver, u32 *wver,
	u64 *body_size, uint *body_off)
{
	if (!data || (u64)off + 0x20 > size || memcmp (data + off, "RFRM", 4))
		return false;
	const u64 fsize = rd_le64 (data + off + 4);
	if (fsize > size || (u64)off + 0x20 + fsize > size)
		return false;
	memcpy (id, data + off + 0x14, 4);
	if (rver)
		*rver = rd_le32 (data + off + 0x18);
	if (wver)
		*wver = rd_le32 (data + off + 0x1c);
	if (body_size)
		*body_size = fsize;
	if (body_off)
		*body_off = off + 0x20;
	return true;
}

static bool mpr_read_chunk (const u8 *data, uint size, uint off, char id[4], u64 *body_size,
	uint *body_off)
{
	if (!data || (u64)off + 0x18 > size)
		return false;
	memcpy (id, data + off, 4);
	const u64 csize = rd_le64 (data + off + 4);
	const u64 skip = rd_le64 (data + off + 0x10);
	if (csize > size || skip > size || (u64)off + 0x18 + skip + csize > size)
		return false;
	if (body_size)
		*body_size = csize;
	if (body_off)
		*body_off = off + 0x18 + (uint)skip;
	return true;
}

enumError ScanMPRTXTR (mpr_txtr_info_t *info, const u8 *data, uint size)
{
	if (!info || !data || size < 0x20)
		return EINVAL;
	memset (info, 0, sizeof (*info));

	//--- outer "TXTR" form ---
	char fid[4];
	u32 rver, wver;
	u64 fsize;
	uint fbody;
	if (!mpr_read_form (data, size, 0, fid, &rver, &wver, &fsize, &fbody) || memcmp (fid, "TXTR", 4))
		return EINVAL;
	// The discriminator against Tropical Freeze, which shares this exact
	// RFRM+"TXTR" shell byte-for-byte but never sets this pair: retrotool's
	// txtr.rs hard-asserts reader/writer version 47/51 for MPR.
	if (rver != 47 || wver != 51)
		return EINVAL;
	const uint txtr_end = fbody + (uint)fsize;

	//--- HEAD chunk ---
	char hid[4];
	u64 hsize;
	uint hbody;
	if (!mpr_read_chunk (data, size, fbody, hid, &hsize, &hbody) || memcmp (hid, "HEAD", 4)
		|| hsize < 0x20 + 10 || (u64)hbody + hsize > txtr_end)
		return EINVAL;

	const u8 *hb = data + hbody;
	const uint tex_type = rd_le32 (hb + 0x00);
	const uint tex_format = rd_le32 (hb + 0x04);
	const uint w = rd_le32 (hb + 0x08);
	const uint h = rd_le32 (hb + 0x0c);
	const uint layers = rd_le32 (hb + 0x10);
	const uint mip_count = rd_le32 (hb + 0x1c);
	if (!w || !h || w > MPR_MAX_DIM || h > MPR_MAX_DIM || !layers || layers > 2048 || !mip_count
		|| mip_count > MPR_MAX_MIPS)
		return EINVAL;
	// STextureHeader body: 8 LE u32s (0x20), mip_count LE u32 mip sizes,
	// then a 10-byte sampler_data record.
	if ((u64)0x20 + (u64)mip_count * 4 + 10 > hsize)
		return EINVAL;

	//--- GPU chunk (its bytes are addressed via META's absolute file
	// offsets below, not by walking through here; only its presence and
	// bounds matter to the scan) ---
	char gid[4];
	u64 gsize;
	uint gbody;
	if (!mpr_read_chunk (data, size, hbody + (uint)hsize, gid, &gsize, &gbody)
		|| memcmp (gid, "GPU ", 4) || (u64)gbody + gsize > txtr_end)
		return EINVAL;

	//--- FOOT form: a sibling top-level RFRM immediately after the TXTR
	// form (not nested inside it -- the TXTR form's own size does not
	// cover it), holding an AINF chunk and then the META chunk we need. ---
	char footid[4];
	u32 frver, fwver;
	u64 footsize;
	uint footbody;
	if (!mpr_read_form (data, size, txtr_end, footid, &frver, &fwver, &footsize, &footbody))
		return EINVAL;
	const uint foot_end = footbody + (uint)footsize;

	const u8 *meta = 0;
	uint meta_size = 0;
	for (uint pos = footbody; pos < foot_end;)
	{
		char cid[4];
		u64 csize;
		uint cbody;
		if (!mpr_read_chunk (data, size, pos, cid, &csize, &cbody) || (u64)cbody + csize > foot_end)
			return EINVAL;
		if (!memcmp (cid, "META", 4))
		{
			meta = data + cbody;
			meta_size = (uint)csize;
			break;
		}
		pos = cbody + (uint)csize;
	}
	if (!meta)
		return EINVAL;

	//--- META payload: unk1,unk2,alloc_category,gpu_offset,align,
	// decompressed_size,info_count, info[info_count] (u8 index + LE u32
	// offset + LE u32 size, 9 bytes each), buffer_count, buffers[
	// buffer_count] (5 LE u32: index,offset,size,dest_offset,dest_size,
	// 20 bytes each). All absolute offsets below are into the whole file. ---
	if (meta_size < 28)
		return EINVAL;
	const u32 decomp_size = rd_le32 (meta + 20);
	const u32 info_count = rd_le32 (meta + 24);
	if (!decomp_size || decomp_size > RETRO_TXTR_MAX_OUTPUT || info_count > MPR_MAX_INFOS)
		return EINVAL;
	uint p = 28;
	if ((u64)p + (u64)info_count * 9 + 4 > meta_size)
		return EINVAL;
	const u8 *info_arr = meta + p;
	p += info_count * 9;
	const u32 buf_count = rd_le32 (meta + p);
	p += 4;
	if (!buf_count || buf_count > MPR_MAX_BUFFERS || (u64)p + (u64)buf_count * 20 > meta_size)
		return EINVAL;
	const u8 *buf_arr = meta + p;

	for (uint i = 0; i < buf_count; i++)
	{
		const u8 *b = buf_arr + i * 20;
		const uint b_index = rd_le32 (b + 0);
		const uint b_offset = rd_le32 (b + 4);
		const uint b_size = rd_le32 (b + 8);
		const uint dest_offset = rd_le32 (b + 12);
		const uint dest_size = rd_le32 (b + 16);
		if (!b_size || !dest_size || (u64)dest_offset + dest_size > decomp_size)
			return EINVAL;

		const u8 *found = 0;
		for (uint j = 0; j < info_count; j++)
		{
			const u8 *ie = info_arr + j * 9;
			if (ie[0] == b_index)
			{
				found = ie;
				break;
			}
		}
		if (!found)
			return EINVAL;
		const uint i_offset = rd_le32 (found + 1);
		const uint i_size = rd_le32 (found + 5);
		if ((u64)b_offset + b_size > i_size || (u64)i_offset + b_offset + b_size > size)
			return EINVAL;
	}

	info->tex_type = tex_type;
	info->tex_format = tex_format;
	info->width = w;
	info->height = h;
	info->depth = layers;
	info->mip_count = mip_count;
	info->decomp_size = decomp_size;
	info->meta = meta;
	info->meta_size = meta_size;
	return ERR_OK;
}

bool IsMPRTXTR (const u8 *data, uint size)
{
	if (!data || size < 0x20 || memcmp (data, "RFRM", 4))
		return false;
	mpr_txtr_info_t info;
	return ScanMPRTXTR (&info, data, size) == ERR_OK;
}

// retrotool ETextureFormat id -> decode kind + block geometry. Covers the
// uncompressed/BC/ASTC formats actually seen in shipped textures; anything
// else (float/depth/exotic swizzle formats) fails cleanly.
enum
{
	MPR_K_R8,
	MPR_K_RGBA8,
	MPR_K_BC1,
	MPR_K_BC2,
	MPR_K_BC3,
	MPR_K_BC4,
	MPR_K_BC5,
	MPR_K_BC6,
	MPR_K_BC7,
	MPR_K_ASTC
};

static bool mpr_format_info (uint fmt, uint *bpp, uint *blk_w, uint *blk_h, uint *kind,
	bool *is_signed)
{
	static const u8 astc_dim[14][2] = { { 4, 4 }, { 5, 4 }, { 5, 5 }, { 6, 5 }, { 6, 6 }, { 8, 5 },
		{ 8, 6 }, { 8, 8 }, { 10, 5 }, { 10, 6 }, { 10, 8 }, { 10, 10 }, { 12, 10 }, { 12, 12 } };

	*is_signed = false;
	switch (fmt)
	{
		case 0: // R8Unorm
			*bpp = 1;
			*blk_w = *blk_h = 1;
			*kind = MPR_K_R8;
			return true;
		case 12: // Rgba8Unorm
		case 13: // Rgba8Srgb
			*bpp = 4;
			*blk_w = *blk_h = 1;
			*kind = MPR_K_RGBA8;
			return true;
		case 20: // RgbaBc1Unorm
		case 21: // RgbaBc1Srgb
			*bpp = 8;
			*blk_w = *blk_h = 4;
			*kind = MPR_K_BC1;
			return true;
		case 22: // RgbaBc2Unorm
		case 23: // RgbaBc2Srgb
			*bpp = 16;
			*blk_w = *blk_h = 4;
			*kind = MPR_K_BC2;
			return true;
		case 24: // RgbaBc3Unorm
		case 25: // RgbaBc3Srgb
			*bpp = 16;
			*blk_w = *blk_h = 4;
			*kind = MPR_K_BC3;
			return true;
		case 26: // RgbaBc4Unorm
		case 27: // RgbaBc4Snorm
			*bpp = 8;
			*blk_w = *blk_h = 4;
			*kind = MPR_K_BC4;
			*is_signed = fmt == 27;
			return true;
		case 28: // RgbaBc5Unorm
		case 29: // RgbaBc5Snorm
			*bpp = 16;
			*blk_w = *blk_h = 4;
			*kind = MPR_K_BC5;
			*is_signed = fmt == 29;
			return true;
		case 81: // BptcUfloat (BC6H unsigned)
		case 82: // BptcSfloat (BC6H signed)
			*bpp = 16;
			*blk_w = *blk_h = 4;
			*kind = MPR_K_BC6;
			*is_signed = fmt == 82;
			return true;
		case 83: // BptcUnorm (BC7)
		case 84: // BptcUnormSrgb
			*bpp = 16;
			*blk_w = *blk_h = 4;
			*kind = MPR_K_BC7;
			return true;
		default:
			if (fmt >= 53 && fmt <= 80)
			{
				const uint idx = fmt < 67 ? fmt - 53 : fmt - 67;
				*bpp = 16;
				*blk_w = astc_dim[idx][0];
				*blk_h = astc_dim[idx][1];
				*kind = MPR_K_ASTC;
				return true;
			}
			return false;
	}
}

// Ported from tegra_swizzle's blockheight.rs (block_height_mip0(), itself
// ported from Ryujinx's driver-matching C#). Only mip level 0 is decoded
// here, so the mip_block_height() shrink-for-smaller-mips step is not
// needed -- mip 0 always starts fresh from this value.
static uint mpr_block_height_mip0 (uint height_in_blocks)
{
	const uint hh = height_in_blocks + height_in_blocks / 2;
	return hh >= 128 ? 16 : hh >= 64 ? 8 : hh >= 32 ? 4 : hh >= 16 ? 2 : 1;
}

static uint mpr_log2_pow2 (uint v)
{
	uint log2 = 0;
	while (v > 1)
	{
		v >>= 1;
		log2++;
	}
	return log2;
}

enumError DecodeMPRTXTR_RGBA (
	u8 **dest, uint *width, uint *height, const u8 *src, uint src_size)
{
	if (!dest || !width || !height)
		return EINVAL;
	*dest = 0;
	*width = *height = 0;
	mpr_txtr_info_t info;
	if (ScanMPRTXTR (&info, src, src_size))
		return EINVAL;
	if (info.tex_type != 1 || info.depth != 1)
		return EINVAL; // 2D only; cubemaps/arrays/3D fail cleanly

	uint bpp, blk_w, blk_h, kind;
	bool is_signed;
	if (!mpr_format_info (info.tex_format, &bpp, &blk_w, &blk_h, &kind, &is_signed))
		return EINVAL;

	// Re-walk the META buffer table (Scan already bounds-checked every
	// field used here).
	const u8 *meta = info.meta;
	const u32 info_count = rd_le32 (meta + 24);
	uint p = 28;
	const u8 *info_arr = meta + p;
	p += info_count * 9;
	const u32 buf_count = rd_le32 (meta + p);
	p += 4;
	const u8 *buf_arr = meta + p;

	u8 *raw = CALLOC (1, info.decomp_size);
	if (!raw)
		return ERR_CANT_CREATE;
	enumError err = ERR_OK;
	for (uint i = 0; i < buf_count && !err; i++)
	{
		const u8 *b = buf_arr + i * 20;
		const uint b_index = rd_le32 (b + 0);
		const uint b_offset = rd_le32 (b + 4);
		const uint b_size = rd_le32 (b + 8);
		const uint dest_offset = rd_le32 (b + 12);
		const uint dest_size = rd_le32 (b + 16);

		const u8 *found = 0;
		// The buffer's read index normally equals its own position:
		// direct-index that common case, keep the linear scan as
		// fallback so an exotic file decodes exactly as before.
		if (b_index < info_count && info_arr[9 * (u64)b_index] == b_index)
			found = info_arr + 9 * (u64)b_index;
		for (uint j = 0; !found && j < info_count; j++)
		{
			const u8 *ie = info_arr + j * 9;
			if (ie[0] == b_index)
			{
				found = ie;
				break;
			}
		}
		if (!found)
		{
			err = EINVAL;
			break;
		}
		const uint i_offset = rd_le32 (found + 1);
		const u8 *comp = src + i_offset + b_offset;

		u8 *dec = 0;
		uint dec_sz = 0;
		err = tropical_lzss (&dec, &dec_sz, comp, b_size, dest_size);
		if (err)
		{
			FREE (dec);
			break;
		}
		memcpy (raw + dest_offset, dec, dest_size);
		FREE (dec);
	}
	if (err)
	{
		FREE (raw);
		return err;
	}

	// Mip level 0's block-height parameter, then the same Tegra X1
	// block-linear GOB detiler this tree already uses for BNTX.
	const uint bh_blocks = mpr_div_round_up (info.height, blk_h);
	const uint block_height_log2 = mpr_log2_pow2 (mpr_block_height_mip0 (bh_blocks));

	u8 *linear = 0;
	uint linear_size = 0;
	err = BntxDeswizzle (&linear, &linear_size, raw, info.decomp_size, info.width, info.height,
		blk_w, blk_h, bpp, 0, block_height_log2, true);
	FREE (raw);
	if (err)
		return err;

	const uint w = info.width, h = info.height;
	if ((u64)w * h > RETRO_TXTR_MAX_OUTPUT / 4)
	{
		FREE (linear);
		return EFBIG;
	}
	u8 *rgba = CALLOC (1, (size_t)w * h * 4);
	if (!rgba)
	{
		FREE (linear);
		return ERR_CANT_CREATE;
	}

	if (blk_w == 1 && blk_h == 1)
	{
		for (uint y = 0; y < h; y++)
			for (uint x = 0; x < w; x++)
			{
				const u8 *sp = linear + ((size_t)y * w + x) * bpp;
				u8 *dp = rgba + 4 * ((size_t)y * w + x);
				if (kind == MPR_K_R8)
				{
					dp[0] = dp[1] = dp[2] = sp[0];
					dp[3] = 255;
				}
				else // MPR_K_RGBA8
					memcpy (dp, sp, 4);
			}
	}
	else if (kind == MPR_K_BC6 || kind == MPR_K_BC7)
	{
		const int ok = kind == MPR_K_BC6 ? szs_decode_bc6 (linear, w, h, is_signed, rgba)
										  : szs_decode_bc7 (linear, w, h, rgba);
		if (!ok)
		{
			FREE (rgba);
			FREE (linear);
			return ERR_INVALID_DATA;
		}
	}
	else
	{
		const uint bwc = mpr_div_round_up (w, blk_w), bhc = mpr_div_round_up (h, blk_h);
		for (uint by = 0; by < bhc; by++)
			for (uint bx = 0; bx < bwc; bx++)
			{
				const u8 *blk = linear + ((size_t)by * bwc + bx) * bpp;
				u8 px[12 * 12 * 4];
				switch (kind)
				{
					case MPR_K_BC1:
						decode_bc1_block (blk, px, true);
						break;
					case MPR_K_BC2:
						decode_bc2_block (blk, px);
						break;
					case MPR_K_BC3:
						decode_bc3_block (blk, px);
						break;
					case MPR_K_BC4:
						is_signed ? decode_bc4_signed_block (blk, px) : decode_bc4_block (blk, px);
						break;
					case MPR_K_BC5:
						is_signed ? decode_bc5_signed_block (blk, px) : decode_bc5_block (blk, px);
						break;
					case MPR_K_ASTC:
						astc_decompress_block (px, blk, blk_w, blk_h);
						break;
					default:
						memset (px, 0, sizeof (px));
						break;
				}
				for (uint iy = 0; iy < blk_h; iy++)
					for (uint ix = 0; ix < blk_w; ix++)
					{
						const uint x = bx * blk_w + ix, y = by * blk_h + iy;
						if (x >= w || y >= h)
							continue;
						memcpy (rgba + 4 * ((size_t)y * w + x), px + 4 * (iy * blk_w + ix), 4);
					}
			}
	}

	FREE (linear);
	*dest = rgba;
	*width = w;
	*height = h;
	return ERR_OK;
}
