// SPDX-License-Identifier: GPL-2.0+
#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-mpr-pak.h"
#include "lib-mpr-txtr.h"
#include "lib-bntx.h"
#include "astc/astc_wrapper.h"
#include "bcn-decoder/bcn_wrapper.h"
#include <string.h>

//-----------------------------------------------------------------------------
// format table
//-----------------------------------------------------------------------------

typedef enum mpr_pxkind_t
{
	PX_UNSUPPORTED = 0,
	PX_R8,
	PX_R16,
	PX_R32,
	PX_R32F,
	PX_RGB8,
	PX_RGBA8,
	PX_RGBA16F,
	PX_RGBA32F,
	PX_RG8,
	PX_RG16F,
	PX_RGB10A2,
	PX_R11G11B10F,
	PX_RGBA16U,
	PX_D16,
	PX_D24S8,
	PX_D32F,
	PX_BC1,
	PX_BC2,
	PX_BC3,
	PX_BC4,
	PX_BC4_SNORM,
	PX_BC5,
	PX_BC5_SNORM,
	PX_BC6H,
	PX_BC6H_SNORM,
	PX_BC7,
	PX_ASTC,
} mpr_pxkind_t;

typedef struct mpr_format_t
{
	u8 bw, bh; // block footprint in pixels
	uint bpp; // bytes per block (1x1 for uncompressed)
	mpr_pxkind_t kind;
	u8 astc_w, astc_h; // valid for PX_ASTC
} mpr_format_t;

static bool mpr_format_info (uint fmt, mpr_format_t *fi)
{
	if (!fi)
		return false;
	switch (fmt)
	{
		case 0:
			*fi = (mpr_format_t) { 1, 1, 1, PX_R8, 0, 0 };
			return true;
		case 4:
			*fi = (mpr_format_t) { 1, 1, 2, PX_R16, 0, 0 };
			return true;
		case 9:
			*fi = (mpr_format_t) { 1, 1, 4, PX_R32, 0, 0 };
			return true;
		case 11:
			*fi = (mpr_format_t) { 1, 1, 3, PX_RGB8, 0, 0 };
			return true;
		case 12:
		case 13:
			*fi = (mpr_format_t) { 1, 1, 4, PX_RGBA8, 0, 0 };
			return true;
		case 14:
			*fi = (mpr_format_t) { 1, 1, 8, PX_RGBA16F, 0, 0 };
			return true;
		case 15:
			*fi = (mpr_format_t) { 1, 1, 16, PX_RGBA32F, 0, 0 };
			return true;
		case 16:
		case 17:
			*fi = (mpr_format_t) { 1, 1, 2, PX_D16, 0, 0 };
			return true;
		case 18:
			*fi = (mpr_format_t) { 1, 1, 4, PX_D24S8, 0, 0 };
			return true;
		case 19:
			*fi = (mpr_format_t) { 1, 1, 4, PX_D32F, 0, 0 };
			return true;
		case 20:
		case 21:
			*fi = (mpr_format_t) { 4, 4, 8, PX_BC1, 0, 0 };
			return true;
		case 22:
		case 23:
			*fi = (mpr_format_t) { 4, 4, 16, PX_BC2, 0, 0 };
			return true;
		case 24:
		case 25:
			*fi = (mpr_format_t) { 4, 4, 16, PX_BC3, 0, 0 };
			return true;
		case 26:
			*fi = (mpr_format_t) { 4, 4, 8, PX_BC4, 0, 0 };
			return true;
		case 27:
			*fi = (mpr_format_t) { 4, 4, 8, PX_BC4_SNORM, 0, 0 };
			return true;
		case 28:
			*fi = (mpr_format_t) { 4, 4, 16, PX_BC5, 0, 0 };
			return true;
		case 29:
			*fi = (mpr_format_t) { 4, 4, 16, PX_BC5_SNORM, 0, 0 };
			return true;
		case 30:
			*fi = (mpr_format_t) { 1, 1, 4, PX_R11G11B10F, 0, 0 };
			return true;
		case 31:
			*fi = (mpr_format_t) { 1, 1, 4, PX_R32F, 0, 0 };
			return true;
		case 32:
			*fi = (mpr_format_t) { 1, 1, 2, PX_RG8, 0, 0 };
			return true;
		case 36:
			*fi = (mpr_format_t) { 1, 1, 4, PX_RG16F, 0, 0 };
			return true;
		case 41:
		case 42:
			*fi = (mpr_format_t) { 1, 1, 4, PX_RGB10A2, 0, 0 };
			return true;
		case 46:
			*fi = (mpr_format_t) { 1, 1, 8, PX_RGBA16U, 0, 0 };
			return true;
		case 81:
			*fi = (mpr_format_t) { 4, 4, 16, PX_BC6H, 0, 0 };
			return true;
		case 82:
			*fi = (mpr_format_t) { 4, 4, 16, PX_BC6H_SNORM, 0, 0 };
			return true;
		case 83:
		case 84:
			*fi = (mpr_format_t) { 4, 4, 16, PX_BC7, 0, 0 };
			return true;
		default:
			break;
	}
	// ASTC 53..66 + sRGB twins 67..80.
	if ((fmt >= 53 && fmt <= 66) || (fmt >= 67 && fmt <= 80))
	{
		static const u8 dims[14][2] = { { 4, 4 }, { 5, 4 }, { 5, 5 }, { 6, 5 }, { 6, 6 },
			{ 8, 5 }, { 8, 6 }, { 8, 8 }, { 10, 5 }, { 10, 6 }, { 10, 8 }, { 10, 10 },
			{ 12, 10 }, { 12, 12 } };
		const uint ix = (fmt <= 66 ? fmt : fmt - 14) - 53;
		*fi = (mpr_format_t) { dims[ix][0], dims[ix][1], 16, PX_ASTC, dims[ix][0],
			dims[ix][1] };
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// RFRM/HEAD probe + scan
//-----------------------------------------------------------------------------

static bool read_le_form (const u8 *data, uint size, uint off, char id[4], u32 *rver,
	u32 *wver, u64 *body_size, uint *body_off)
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

static bool read_le_chunk (const u8 *data, uint size, uint off, char id[4], u64 *body_size,
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

// HEAD body layout: 8xu32, mip_count xu32, u32 sampler unk, 6xu8 sampler.
static bool parse_head (const u8 *data, uint size, uint hbody, u64 hsize, mpr_txtr_info_t *info,
	uint *mip0_size, const u8 **mip_table)
{
	if ((u64)hsize < 0x20 + 4 + 10 || (u64)hbody + hsize > size)
		return false;
	const uint kind = rd_le32 (data + hbody);
	const uint format = rd_le32 (data + hbody + 4);
	const uint w = rd_le32 (data + hbody + 8);
	const uint h = rd_le32 (data + hbody + 12);
	const uint layers = rd_le32 (data + hbody + 16);
	const uint tile = rd_le32 (data + hbody + 20);
	const uint mc = rd_le32 (data + hbody + 28);
	if (!w || !h || w > MPR_TXTR_MAX_DIM || h > MPR_TXTR_MAX_DIM || !layers
		|| layers > MPR_TXTR_MAX_LAYERS || tile != 0 || !mc || mc > MPR_TXTR_MAX_MIPS)
		return false;
	if (kind == 2 || kind == 6 || kind == 7)
		return false; // 3D + multisampled need paths not implemented here
	mpr_format_t fi;
	if (!mpr_format_info (format, &fi))
		return false;
	if ((u64)0x20 + (u64)mc * 4 + 10 > hsize)
		return false;
	if (mip_table)
		*mip_table = data + hbody + 0x20;
	if (mip0_size)
		*mip0_size = rd_le32 (data + hbody + 0x20);
	if (info)
	{
		info->kind = kind;
		info->format = format;
		info->width = w;
		info->height = h;
		info->layers = layers;
		info->tile_mode = tile;
		info->mip_count = mc;
	}
	return true;
}

bool IsMPRTXTR (const u8 *data, uint size)
{
	if (!data || size < 0x20 + 0x18 + 0x20)
		return false;
	char fid[4];
	u32 rver;
	u64 fsize;
	uint fbody;
	if (!read_le_form (data, size, 0, fid, &rver, 0, &fsize, &fbody)
		|| memcmp (fid, "TXTR", 4) || (rver != 47 && rver != 51))
		return false;
	char cid[4];
	u64 csize;
	uint cbody;
	if (!read_le_chunk (data, size, fbody, cid, &csize, &cbody) || memcmp (cid, "HEAD", 4))
		return false;
	return parse_head (data, size, cbody, csize, 0, 0, 0);
}

//--- FOOT META ----------------------------------------------------------
// STextureMetaData: 6xu32, u32 info_count, infos[{u8 idx, u32 off, u32 size}
// = 9 bytes], u32 buffer_count, buffers[5xu32]. All offsets address the
// whole input file (resource bytes first, matching the reference).

static enumError parse_meta (const u8 *data, uint size, uint mbody, u64 msize, uint *decomp_size,
	const u8 **read_base, uint *n_reads, const u8 **buf_base, uint *n_bufs)
{
	if ((u64)msize < 28 || (u64)mbody + msize > size)
		return EINVAL;
	const uint ninfo = rd_le32 (data + mbody + 24);
	if ((u64)28 + (u64)ninfo * 9 + 4 > msize)
		return EINVAL;
	uint pos = mbody + 28 + ninfo * 9;
	if ((u64)pos + 4 > (u64)mbody + msize)
		return EINVAL;
	const uint nbuf = rd_le32 (data + pos);
	pos += 4;
	if ((u64)nbuf * 20 > (u64)mbody + msize - pos)
		return EINVAL;
	if (decomp_size)
		*decomp_size = rd_le32 (data + mbody + 20);
	if (read_base)
		*read_base = data + mbody + 28;
	if (n_reads)
		*n_reads = ninfo;
	if (buf_base)
		*buf_base = data + pos;
	if (n_bufs)
		*n_bufs = nbuf;
	(void)ninfo;
	return ERR_OK;
}

static enumError find_meta (const u8 *data, uint size, uint *mbody, u64 *msize)
{
	// FOOT form follows the resource form (which ends at 0x20 + size).
	char fid[4];
	u64 fsize;
	uint fbody;
	if (!read_le_form (data, size, 0, fid, 0, 0, &fsize, &fbody) || memcmp (fid, "TXTR", 4))
		return EINVAL;
	uint pos = fbody + (uint)fsize;
	char ffid[4];
	u32 frver, fwver;
	u64 fsize2;
	uint fbody2;
	if (!read_le_form (data, size, pos, ffid, &frver, &fwver, &fsize2, &fbody2)
		|| memcmp (ffid, "FOOT", 4))
		return EINVAL;
	uint cpos = fbody2;
	const uint cend = fbody2 + (uint)fsize2;
	while (cpos < cend)
	{
		char cid[4];
		u64 csize;
		uint cbody;
		if (!read_le_chunk (data, size, cpos, cid, &csize, &cbody) || csize > UINT_MAX
			|| (u64)cbody + csize > (u64)fbody2 + fsize2)
			return EINVAL;
		if (!memcmp (cid, "META", 4))
		{
			*mbody = cbody;
			*msize = csize;
			return ERR_OK;
		}
		cpos = cbody + (uint)csize;
	}
	return EINVAL;
}

enumError ScanMPRTXTR (mpr_txtr_info_t *info, const u8 *data, uint size)
{
	if (!info || !data)
		return EINVAL;
	memset (info, 0, sizeof (*info));
	if (!IsMPRTXTR (data, size))
		return EINVAL;
	char cid[4];
	u64 csize;
	uint cbody;
	uint fbody;
	{
		char fid[4];
		u64 fsize;
		read_le_form (data, size, 0, fid, 0, 0, &fsize, &fbody);
	}
	read_le_chunk (data, size, fbody, cid, &csize, &cbody);
	uint mip0 = 0;
	if (!parse_head (data, size, cbody, csize, info, &mip0, 0) || !mip0
		|| mip0 > MPR_TXTR_MAX_OUTPUT)
		return EINVAL;
	uint mbody;
	u64 msize;
	if (find_meta (data, size, &mbody, &msize))
		return EINVAL;
	uint decomp = 0;
	if (parse_meta (data, size, mbody, msize, &decomp, 0, 0, 0, 0) || !decomp
		|| decomp > MPR_TXTR_MAX_OUTPUT)
		return EINVAL;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// Tegra block-linear geometry (tegra_swizzle size model, verified against
// ~7000 retail META sizes; tile_mode/swizzle are constant zero corpus-wide)
//-----------------------------------------------------------------------------

static uint mpr_div_ru (uint x, uint d)
{
	return d ? (x + d - 1) / d : 0;
}

static uint mpr_block_height_mip0 (uint h)
{
	const uint s = h + h / 2;
	if (s >= 128)
		return 16;
	if (s >= 64)
		return 8;
	if (s >= 32)
		return 4;
	if (s >= 16)
		return 2;
	return 1;
}

static uint mpr_mip_block_height (uint mh, uint b0)
{
	uint b = b0;
	while (mh <= (b / 2) * 8 && b > 1)
		b /= 2;
	return b;
}

// Swizzled bytes of one (element-sized) mip level.
static u64 mpr_swizzled_mip_size (uint ew, uint eh, uint bpp, uint bh)
{
	const uint wig = mpr_div_ru (ew * bpp, 64);
	const uint hib = mpr_div_ru (eh, bh * 8);
	return (u64)wig * (hib * bh) * 512;
}

// Full swizzled surface incl. per-layer alignment (depth is 1: kind 2
// rejected in parse_head, cubes/arrays ride the layer loop).
static u64 mpr_swizzled_size (uint w, uint h, uint bw, uint bh, uint bpp, uint mips,
	uint layers)
{
	const uint b0 = mpr_block_height_mip0 (mpr_div_ru (h, bh));
	u64 total = 0;
	uint mw = w, mh = h;
	for (uint m = 0; m < mips; m++)
	{
		uint ew = mpr_div_ru (mw, bw);
		uint eh = mpr_div_ru (mh, bh);
		if (!ew)
			ew = 1;
		if (!eh)
			eh = 1;
		total += mpr_swizzled_mip_size (ew, eh, bpp, mpr_mip_block_height (eh, b0));
		mw >>= 1;
		mh >>= 1;
	}
	if (layers > 1)
	{
		// align_layer_size(total, h, depth=1, b0, 1) per layer.
		uint gh = b0;
		while (h <= (gh / 2) * 8 && gh > 1)
			gh /= 2;
		const u64 blk = (u64)gh * 512;
		const u64 n = total / blk;
		if (total != n * blk)
			total = (n + 1) * blk;
		total *= layers;
	}
	return total;
}

//-----------------------------------------------------------------------------
// pixel decode
//-----------------------------------------------------------------------------

static inline u8 mpr_scale5 (uint v)
{
	return (u8)(v * 255 / 31);
}

static inline u8 mpr_clamp_byte (float v)
{
	return v <= 0 ? 0 : v >= 255 ? 255 : (u8)(v + .5f);
}

static float mpr_half_to_float (u16 h)
{
	const uint sign = h >> 15, exp = (h >> 10) & 31, mant = h & 1023;
	float v;
	if (!exp)
		v = mant / 16777216.0f;
	else if (exp == 31)
		v = mant ? 0.0f : 65504.0f;
	else
	{
		v = 1.0f + mant / 1024.0f;
		int e = (int)exp - 15;
		while (e > 0)
		{
			v *= 2;
			e--;
		}
		while (e < 0)
		{
			v *= .5f;
			e++;
		}
	}
	return sign ? -v : v;
}

static inline u16 mpr_rd_le16 (const u8 *p)
{
	return (u16)p[0] | (u16)p[1] << 8;
}

static inline u32 mpr_rd_le32 (const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static inline float mpr_rd_le_float (const u8 *p)
{
	float v;
	memcpy (&v, p, 4);
	return v;
}

// Decode one deswizzled mip level (ew*eh elements) to w*h RGBA8. Edges
// clip: small mips still decode through the same block walk.
static enumError mpr_decode_pixels (u8 *dst, uint w, uint h, mpr_format_t fi, const u8 *src,
	uint src_size)
{
	const uint ew = mpr_div_ru (w, fi.bw), eh = mpr_div_ru (h, fi.bh);
	if ((u64)ew * eh * fi.bpp > src_size)
		return EINVAL;
	switch (fi.kind)
	{
		case PX_R8:
			for (uint i = 0, n = w * h; i < n; i++)
			{
				dst[4 * i] = dst[4 * i + 1] = dst[4 * i + 2] = src[i];
				dst[4 * i + 3] = 255;
			}
			return ERR_OK;
		case PX_R16:
			for (uint i = 0, n = w * h; i < n; i++)
			{
				const u8 v = (u8)(mpr_rd_le16 (src + 2 * i) * 255 / 65535);
				dst[4 * i] = dst[4 * i + 1] = dst[4 * i + 2] = v;
				dst[4 * i + 3] = 255;
			}
			return ERR_OK;
		case PX_R32:
			for (uint i = 0, n = w * h; i < n; i++)
			{
				const u32 v = mpr_rd_le32 (src + 4 * i);
				const u8 b = v >= 0xffffffffu ? 255 : (u8)(((u64)v * 255) / 0xffffffffu);
				dst[4 * i] = dst[4 * i + 1] = dst[4 * i + 2] = b;
				dst[4 * i + 3] = 255;
			}
			return ERR_OK;
		case PX_R32F:
			for (uint i = 0, n = w * h; i < n; i++)
			{
				const u8 v = mpr_clamp_byte (mpr_rd_le_float (src + 4 * i) * 255.0f);
				dst[4 * i] = dst[4 * i + 1] = dst[4 * i + 2] = v;
				dst[4 * i + 3] = 255;
			}
			return ERR_OK;
		case PX_RGB8:
			for (uint i = 0, n = w * h; i < n; i++)
			{
				dst[4 * i] = src[3 * i];
				dst[4 * i + 1] = src[3 * i + 1];
				dst[4 * i + 2] = src[3 * i + 2];
				dst[4 * i + 3] = 255;
			}
			return ERR_OK;
		case PX_RGBA8:
			memcpy (dst, src, (size_t)w * h * 4);
			return ERR_OK;
		case PX_RGBA16F:
			for (uint i = 0, n = w * h; i < n; i++)
				for (uint c = 0; c < 4; c++)
					dst[4 * i + c] = mpr_clamp_byte (
						mpr_half_to_float (mpr_rd_le16 (src + 8 * i + 2 * c)) * 255.0f);
			return ERR_OK;
		case PX_RGBA32F:
			for (uint i = 0, n = w * h; i < n; i++)
				for (uint c = 0; c < 4; c++)
					dst[4 * i + c]
						= mpr_clamp_byte (mpr_rd_le_float (src + 16 * i + 4 * c) * 255.0f);
			return ERR_OK;
		case PX_RG8:
			for (uint i = 0, n = w * h; i < n; i++)
			{
				dst[4 * i] = src[2 * i];
				dst[4 * i + 1] = src[2 * i + 1];
				dst[4 * i + 2] = 0;
				dst[4 * i + 3] = 255;
			}
			return ERR_OK;
		case PX_RG16F:
			for (uint i = 0, n = w * h; i < n; i++)
			{
				for (uint c = 0; c < 2; c++)
					dst[4 * i + c] = mpr_clamp_byte (
						mpr_half_to_float (mpr_rd_le16 (src + 4 * i + 2 * c)) * 255.0f);
				dst[4 * i + 2] = 0;
				dst[4 * i + 3] = 255;
			}
			return ERR_OK;
		case PX_RGB10A2:
			for (uint i = 0, n = w * h; i < n; i++)
			{
				const u32 v = mpr_rd_le32 (src + 4 * i);
				dst[4 * i] = (u8)(((v >> 20) & 1023) * 255 / 1023);
				dst[4 * i + 1] = (u8)(((v >> 10) & 1023) * 255 / 1023);
				dst[4 * i + 2] = (u8)((v & 1023) * 255 / 1023);
				dst[4 * i + 3] = (u8)(((v >> 30) & 3) * 85);
			}
			return ERR_OK;
		case PX_R11G11B10F:
		{
			// Shared-exponent-less packed floats: 6-bit mantissas with
			// per-channel exponents, after the Tegra TRM packing.
			for (uint i = 0, n = w * h; i < n; i++)
			{
				const u32 v = mpr_rd_le32 (src + 4 * i);
				const uint rm = v >> 21, gm = (v >> 10) & 0x7ff, bm = v & 0x3ff;
				float rf = (float)(rm & 0x3f) / 64.0f, gf = (float)(gm & 0x3f) / 64.0f,
					  bf = (float)(bm & 0x1f) / 32.0f;
				int re = (int)(rm >> 6) - 15, ge = (int)(gm >> 6) - 15,
					be = (int)(bm >> 5) - 15;
				while (re > 0)
				{
					rf *= 2;
					re--;
				}
				while (re < 0)
				{
					rf *= .5f;
					re++;
				}
				while (ge > 0)
				{
					gf *= 2;
					ge--;
				}
				while (ge < 0)
				{
					gf *= .5f;
					ge++;
				}
				while (be > 0)
				{
					bf *= 2;
					be--;
				}
				while (be < 0)
				{
					bf *= .5f;
					be++;
				}
				dst[4 * i] = mpr_clamp_byte (rf * 255.0f);
				dst[4 * i + 1] = mpr_clamp_byte (gf * 255.0f);
				dst[4 * i + 2] = mpr_clamp_byte (bf * 255.0f);
				dst[4 * i + 3] = 255;
			}
			return ERR_OK;
		}
		case PX_RGBA16U:
			for (uint i = 0, n = w * h; i < n; i++)
				for (uint c = 0; c < 4; c++)
					dst[4 * i + c]
						= (u8)(mpr_rd_le16 (src + 8 * i + 2 * c) * 255 / 65535);
			return ERR_OK;
		case PX_D16:
			for (uint i = 0, n = w * h; i < n; i++)
			{
				const u8 v = (u8)(mpr_rd_le16 (src + 2 * i) * 255 / 65535);
				dst[4 * i] = dst[4 * i + 1] = dst[4 * i + 2] = v;
				dst[4 * i + 3] = 255;
			}
			return ERR_OK;
		case PX_D24S8:
			for (uint i = 0, n = w * h; i < n; i++)
			{
				const u32 v = mpr_rd_le32 (src + 4 * i);
				const u8 z = (u8)(((v >> 8) & 0xffffff) * 255 / 0xffffff);
				dst[4 * i] = dst[4 * i + 1] = dst[4 * i + 2] = z;
				dst[4 * i + 3] = (u8)v;
			}
			return ERR_OK;
		case PX_D32F:
			for (uint i = 0, n = w * h; i < n; i++)
			{
				const u8 v = mpr_clamp_byte (mpr_rd_le_float (src + 4 * i) * 255.0f);
				dst[4 * i] = dst[4 * i + 1] = dst[4 * i + 2] = v;
				dst[4 * i + 3] = 255;
			}
			return ERR_OK;
		case PX_BC1:
		case PX_BC2:
		case PX_BC3:
		case PX_BC4:
		case PX_BC4_SNORM:
		case PX_BC5:
		case PX_BC5_SNORM:
		{
			const uint bw = (w + 3) / 4, bh = (h + 3) / 4;
			for (uint by = 0; by < bh; by++)
				for (uint bx = 0; bx < bw; bx++)
				{
					u8 px[64];
					const u8 *blk = src + (by * bw + bx) * fi.bpp;
					switch (fi.kind)
					{
						case PX_BC1:
							decode_bc1_block (blk, px, true);
							break;
						case PX_BC2:
							decode_bc2_block (blk, px);
							break;
						case PX_BC3:
							decode_bc3_block (blk, px);
							break;
						case PX_BC4:
							decode_bc4_block (blk, px);
							break;
						case PX_BC4_SNORM:
							decode_bc4_signed_block (blk, px);
							break;
						case PX_BC5:
							decode_bc5_block (blk, px);
							break;
						default:
							decode_bc5_signed_block (blk, px);
							break;
					}
					for (uint py = 0; py < 4; py++)
					{
						const uint dy = by * 4 + py;
						if (dy >= h)
							break;
						for (uint xi = 0; xi < 4; xi++)
						{
							const uint dx = bx * 4 + xi;
							if (dx >= w)
								break;
							memcpy (dst + (dy * w + dx) * 4, px + (py * 4 + xi) * 4, 4);
						}
					}
				}
			return ERR_OK;
		}
		case PX_BC6H:
		case PX_BC6H_SNORM:
		case PX_BC7:
		{
			// Decode straight into the caller's buffer (as the BNTX
			// path does): no scratch alloc plus copy. On failure the
			// caller frees dst, so partial contents are harmless.
			int ok;
			if (fi.kind == PX_BC7)
				ok = szs_decode_bc7 (src, w, h, dst);
			else
				ok = szs_decode_bc6 (src, w, h, fi.kind == PX_BC6H_SNORM, dst);
			return ok ? ERR_OK : EINVAL;
		}
		case PX_ASTC:
		{
			const uint bw = (w + fi.astc_w - 1) / fi.astc_w;
			const uint bh = (h + fi.astc_h - 1) / fi.astc_h;
			for (uint by = 0; by < bh; by++)
				for (uint bx = 0; bx < bw; bx++)
				{
					u8 px[576];
					const u8 *blk = src + (by * bw + bx) * 16;
					astc_decompress_block (px, blk, fi.astc_w, fi.astc_h);
					for (uint py = 0; py < fi.astc_h; py++)
					{
						const uint dy = by * fi.astc_h + py;
						if (dy >= h)
							break;
						for (uint xi = 0; xi < fi.astc_w; xi++)
						{
							const uint dx = bx * fi.astc_w + xi;
							if (dx >= w)
								break;
							memcpy (dst + (dy * w + dx) * 4,
								px + (py * fi.astc_w + xi) * 4, 4);
						}
					}
				}
			return ERR_OK;
		}
		default:
			return EINVAL;
	}
}

//-----------------------------------------------------------------------------
// full decode
//-----------------------------------------------------------------------------

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
	mpr_format_t fi;
	if (!mpr_format_info (info.format, &fi))
		return EINVAL;

	// Re-parse the bits ScanMPRTXTR already validated: HEAD mip table +
	// FOOT META directory.
	char cid[4];
	u64 csize;
	uint cbody, fbody;
	{
		char fid[4];
		u64 fsize;
		read_le_form (src, src_size, 0, fid, 0, 0, &fsize, &fbody);
	}
	read_le_chunk (src, src_size, fbody, cid, &csize, &cbody);
	uint mip0_size = 0;
	parse_head (src, src_size, cbody, csize, 0, &mip0_size, 0);
	uint mbody;
	u64 msize;
	if (find_meta (src, src_size, &mbody, &msize))
		return EINVAL;
	uint meta_decomp = 0;
	const u8 *read_base = 0, *buf_base = 0;
	uint n_reads = 0, n_bufs = 0;
	if (parse_meta (src, src_size, mbody, msize, &meta_decomp, &read_base, &n_reads, &buf_base,
			&n_bufs)
		|| !meta_decomp)
		return EINVAL;

	// Assemble the swizzled surface: each buffer decompresses into its
	// destination window (multi-buffer streaming layouts concatenate).
	u8 *swiz = CALLOC (1, meta_decomp);
	if (!swiz)
		return ERR_CANT_CREATE;
	enumError err = ERR_OK;
	for (uint b = 0; b < n_bufs && !err; b++)
	{
		const u8 *bp = buf_base + 20 * b;
		const uint bindex = mpr_rd_le32 (bp);
		const uint boff = mpr_rd_le32 (bp + 4);
		const uint bsize = mpr_rd_le32 (bp + 8);
		const uint bdoff = mpr_rd_le32 (bp + 12);
		const uint bdsize = mpr_rd_le32 (bp + 16);
		const u8 *rp = 0;
		// Reference: the buffer's read index must equal its own
		// position. Direct-index that common case; keep the linear
		// scan as fallback so a file that somehow breaks the
		// invariant decodes exactly as before.
		if (bindex < n_reads)
		{
			const u8 *q = read_base + 9 * (u64)bindex;
			if (q[0] == bindex)
				rp = q;
		}
		for (uint r = 0; rp == 0 && r < n_reads; r++)
		{
			// Reference: first info whose index matches, whose
			// position must equal the index as well.
			const u8 *q = read_base + 9 * (u64)r;
			if (bindex == r && q[0] == r)
			{
				rp = q;
				break;
			}
		}
		if (!rp || (u64)bdoff + bdsize > meta_decomp)
		{
			err = EINVAL;
			break;
		}
		const uint roff = mpr_rd_le32 (rp + 1);
		const uint rsize = mpr_rd_le32 (rp + 5);
		if ((u64)roff + boff + bsize < boff || (u64)roff + boff + bsize < rsize
			|| (u64)roff + boff + bsize > src_size || boff + bsize < boff
			|| boff + bsize > rsize)
		{
			err = EINVAL;
			break;
		}
		u8 *dec = 0;
		uint dec_size = 0;
		err = DecodeMPR_LZSS (&dec, &dec_size, src + roff + boff, bsize, bdsize);
		if (err)
			break;
		memcpy (swiz + bdoff, dec, bdsize);
		FREE (dec);
	}
	if (err)
	{
		FREE (swiz);
		return err;
	}

	// The assembled size must be exactly the Tegra-predicted swizzled
	// surface; anything else is a corrupt/unsupported layout, not pixels.
	const uint ew0 = mpr_div_ru (info.width, fi.bw), eh0 = mpr_div_ru (info.height, fi.bh);
	if (meta_decomp != mpr_swizzled_size (info.width, info.height, fi.bw, fi.bh, fi.bpp,
			info.mip_count, info.layers)
		|| mip0_size
			!= (info.layers == 1 ? ew0 * eh0 * fi.bpp : ew0 * eh0 * fi.bpp * info.layers))
	{
		FREE (swiz);
		return EINVAL;
	}

	// Detile mip 0, then pixel-decode it. The smaller mips are
	// validated arithmetically in the loop but never deswizzled:
	// they cost ~1/3 extra detile work for bytes freed immediately.
	const uint b0 = mpr_block_height_mip0 (eh0);
	u64 src_off = 0, mip0_ssz = 0;
	uint mw = info.width, mh = info.height;
	for (uint m = 0; m < info.mip_count; m++)
	{
		uint ew = mpr_div_ru (mw, fi.bw), eh = mpr_div_ru (mh, fi.bh);
		if (!ew)
			ew = 1;
		if (!eh)
			eh = 1;
		const uint mbh = mpr_mip_block_height (eh, b0);
		const u64 ssz = mpr_swizzled_mip_size (ew, eh, fi.bpp, mbh);
		if (src_off + ssz > meta_decomp)
		{
			FREE (swiz);
			return EINVAL;
		}
		if (!m)
			mip0_ssz = ssz;
		src_off += ssz;
		mw >>= 1;
		mh >>= 1;
	}
	const uint mbh0 = mpr_mip_block_height (eh0, b0);
	const uint blog2 = mbh0 == 16 ? 4 : mbh0 == 8 ? 3 : mbh0 == 4 ? 2 : mbh0 == 2 ? 1 : 0;
	u8 *linear = 0;
	uint linear_size = 0;
	err = BntxDeswizzle (&linear, &linear_size, swiz, (uint)mip0_ssz, ew0 * fi.bw,
		eh0 * fi.bh, fi.bw, fi.bh, fi.bpp, 0, blog2, false);
	FREE (swiz);
	if (err)
	{
		FREE (linear);
		return err;
	}
	// Mip 0 deswizzled must be exactly ew0*eh0 elements.
	if (!linear || linear_size != (u64)ew0 * eh0 * fi.bpp)
	{
		FREE (linear);
		return EINVAL;
	}
	u8 *rgba = MALLOC ((size_t)info.width * info.height * 4);
	if (!rgba)
	{
		FREE (linear);
		return ERR_CANT_CREATE;
	}
	err = mpr_decode_pixels (rgba, info.width, info.height, fi, linear, linear_size);
	FREE (linear);
	if (err)
	{
		FREE (rgba);
		return EINVAL;
	}
	*dest = rgba;
	*width = info.width;
	*height = info.height;
	return ERR_OK;
}
