// SPDX-License-Identifier: GPL-2.0+
#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-excite.h"
#include "lib-gtx.h"
#include "lib-image.h"
#include "lib-retro-txtr.h"
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
