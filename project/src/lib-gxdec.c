// SPDX-License-Identifier: GPL-2.0+
#include "lib-std.h"
#include "lib-gxdec.h"

static void gxdec_rgb565 (uint v, u8 *rgb)
{
	rgb[0] = ((v >> 11) & 31) * 255 / 31;
	rgb[1] = ((v >> 5) & 63) * 255 / 63;
	rgb[2] = (v & 31) * 255 / 31;
}

u8 *GxDecodeCmprAlpha4 (const u8 *cmpr, const u8 *alpha4, uint w, uint h)
{
	if (!cmpr || !w || !h || (w & 7) || (h & 7) || (u64)w * h * 4 > (1u << 30))
		return 0;
	u8 *out = MALLOC ((size_t)w * h * 4);
	if (!out)
		return 0;

	const u8 *p = cmpr;
	for (uint by = 0; by < h; by += 8)
		for (uint bx = 0; bx < w; bx += 8)
			for (uint sub = 0; sub < 4; sub++, p += 8)
			{
				const uint c0 = p[0] << 8 | p[1], c1 = p[2] << 8 | p[3];
				u8 pal[4][4];
				gxdec_rgb565 (c0, pal[0]);
				gxdec_rgb565 (c1, pal[1]);
				pal[0][3] = pal[1][3] = pal[2][3] = pal[3][3] = 255;
				for (uint k = 0; k < 3; k++)
				{
					if (c0 > c1)
					{
						pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3;
						pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3;
					}
					else
					{
						pal[2][k] = (pal[0][k] + pal[1][k]) / 2;
						pal[3][k] = 0;
					}
				}
				if (c0 <= c1)
					pal[3][3] = 0;
				const uint ox = bx + (sub & 1) * 4, oy = by + (sub >> 1) * 4;
				for (uint y = 0; y < 4; y++)
					for (uint x = 0; x < 4; x++)
					{
						const uint idx = (p[4 + y] >> (6 - 2 * x)) & 3;
						u8 *d = out + ((size_t)(oy + y) * w + ox + x) * 4;
						memcpy (d, pal[idx], 4);
					}
			}

	if (alpha4)
	{
		const u8 *a = alpha4;
		for (uint by = 0; by < h; by += 8)
			for (uint bx = 0; bx < w; bx += 8)
				for (uint y = 0; y < 8; y++)
					for (uint x = 0; x < 8; x += 2, a++)
					{
						u8 *d = out + ((size_t)(by + y) * w + bx + x) * 4;
						d[3] = (*a >> 4) * 17;
						d[7] = (*a & 15) * 17;
					}
	}
	return out;
}
