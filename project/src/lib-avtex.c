// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Avalanche .thb / .tbb texture decoder; see lib-avtex.h.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-avtex.h"
#include "lib-excite.h"

static u32 av_rd32 (const u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint av_rd16 (const u8 *p) { return p[0] << 8 | p[1]; }

bool IsAvalancheTexHeader (const u8 *d, size_t size)
{
	if (size != 48 || av_rd32 (d) != 1 || av_rd32 (d + 4) != 0x10)
		return false;
	const uint w = av_rd16 (d + 0x1c), h = av_rd16 (d + 0x1e), fmt = av_rd16 (d + 0x20);
	return w && h && w <= 4096 && h <= 4096 && (fmt <= 6 || fmt == 14);
}

enumError DecodeAvalancheTex (u8 **rgba, uint *width, uint *height, const u8 *thb, size_t thb_size,
	const u8 *tbb, size_t tbb_size)
{
	if (!IsAvalancheTexHeader (thb, thb_size))
		return ERR_NOTHING_TO_DO;
	const uint w = av_rd16 (thb + 0x1c), h = av_rd16 (thb + 0x1e), fmt = av_rd16 (thb + 0x20);
	const enumError err = DecodeGXTexture_RGBA (rgba, w, h, fmt, tbb, tbb_size, 0, 0, 0);
	if (!err)
	{
		*width = w;
		*height = h;
	}
	return err;
}
