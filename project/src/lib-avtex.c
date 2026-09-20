// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Avalanche .thb / .tbb texture decoder; see lib-avtex.h.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-avtex.h"
#include "lib-excite.h"

#define AV_MAX_TEXTURES 256

static u32 av_rd32 (const u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint av_rd16 (const u8 *p) { return p[0] << 8 | p[1]; }

static bool av_record_ok (const u8 *d, size_t size, uint i)
{
	const u8 *t = d + 4 + 12 * (size_t)i;
	const u32 rec = av_rd32 (t);
	if (rec < 4 + 12ull * av_rd32 (d) || rec + 32ull > size)
		return false;
	const uint w = av_rd16 (d + rec + 12), h = av_rd16 (d + rec + 14), fmt = av_rd16 (d + rec + 16);
	return w && h && w <= 4096 && h <= 4096 && (fmt <= 6 || fmt == 14);
}

uint AvalancheTexCount (const u8 *d, size_t size)
{
	if (size < 4 + 12 + 32)
		return 0;
	const u32 n = av_rd32 (d);
	if (!n || n > AV_MAX_TEXTURES || 4 + 12ull * n > size)
		return 0;
	for (uint i = 0; i < n; i++)
		if (!av_record_ok (d, size, i))
			return 0;
	return n;
}

bool IsAvalancheTexHeader (const u8 *d, size_t size)
{
	return AvalancheTexCount (d, size) != 0;
}

enumError DecodeAvalancheTex (u8 **rgba, uint *width, uint *height, const u8 *thb, size_t thb_size,
	const u8 *tbb, size_t tbb_size, uint index)
{
	if (index >= AvalancheTexCount (thb, thb_size))
		return ERR_NOTHING_TO_DO;
	const u8 *t = thb + 4 + 12 * (size_t)index;
	const u32 rec = av_rd32 (t), off = av_rd32 (t + 4), bytes = av_rd32 (t + 8);
	if ((u64)off + bytes > tbb_size)
		return ERR_NOTHING_TO_DO;
	const uint w = av_rd16 (thb + rec + 12), h = av_rd16 (thb + rec + 14), fmt = av_rd16 (thb + rec + 16);
	const enumError err = DecodeGXTexture_RGBA (rgba, w, h, fmt, tbb + off, bytes, 0, 0, 0);
	if (!err)
	{
		*width = w;
		*height = h;
	}
	return err;
}
