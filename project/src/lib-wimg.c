#include "lib-std.h"
#include "lib-wimg.h"
#include <string.h>
#include <errno.h>

#define WIMG_HDR 0x20
#define WIMG_MAX_DIM 4096

static inline u32 wimg_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static inline u16 wimg_be16 (const u8 *p)
{
	return (u16)(p[0] << 8 | p[1]);
}

// GX texture tiling: pixels are stored tile by tile (row-major tiles, each
// tile row-major inside). Walk the linear stream and map it to (x,y).
enumError DecodeWIMG (u8 **rgba_out, uint *width_out, uint *height_out,
	const u8 *data, uint size)
{
	if (!rgba_out || !data || size < WIMG_HDR + 4 || memcmp (data, "WIMG", 4))
		return EINVAL;

	const uint w = wimg_be16 (data + 0x08);
	const uint h = wimg_be16 (data + 0x0a);
	const u8 fmt = data[0x0c];
	u32 off = wimg_be32 (data + 0x10);
	if (!off)
		off = WIMG_HDR;
	if (!w || !h || w > WIMG_MAX_DIM || h > WIMG_MAX_DIM || off > size)
		return EINVAL;

	const u64 npix = (u64)w * h;
	u8 *rgba = CALLOC (npix, 4);
	if (!rgba)
		return ERR_CANT_CREATE;

	uint tw, th, bpp, pal_n;
	switch (fmt)
	{
		case 1:  tw = 8; th = 8; bpp = 4;  pal_n = 16;  break; // CI4
		case 2:  tw = 8; th = 4; bpp = 8;  pal_n = 256; break; // CI8
		case 9:  tw = 4; th = 4; bpp = 32; pal_n = 0;   break; // RGBA32
		default:
			FREE (rgba);
			return EINVAL;
	}

	if (fmt == 9)
	{
		// Two 32-byte groups per 4x4 tile: {A,R} pairs then {G,B} pairs.
		u64 pos = off;
		for (uint ty = 0; ty < h; ty += 4)
			for (uint tx = 0; tx < w; tx += 4)
			{
				if (pos + 64 > size)
					goto truncated;
				const u8 *ar = data + pos;
				const u8 *gb = data + pos + 32;
				pos += 64;
				uint k = 0;
				for (uint j = 0; j < 4; j++)
					for (uint i = 0; i < 4; i++, k++)
					{
						const uint x = tx + i, y = ty + j;
						if (x >= w || y >= h)
							continue;
						u8 *px = rgba + ((u64)y * w + x) * 4;
						px[0] = ar[k * 2 + 1]; // R
						px[1] = gb[k * 2];     // G
						px[2] = gb[k * 2 + 1]; // B
						px[3] = ar[k * 2];     // A
					}
			}
		*rgba_out = rgba;
		*width_out = w;
		*height_out = h;
		return ERR_OK;
	}

	// Paletted: indices (GX-tiled) then a tightly packed RGBA8888 palette.
	const u64 idx_bytes = bpp == 8 ? npix : (npix + 1) / 2;
	const u64 pal_off = off + idx_bytes;
	if (pal_off + (u64)pal_n * 4 > size)
	{
		FREE (rgba);
		return EINVAL;
	}
	const u8 *idx = data + off;
	const u8 *pal = data + pal_off;

	u64 n = 0;
	for (uint ty = 0; ty < h; ty += th)
		for (uint tx = 0; tx < w; tx += tw)
			for (uint j = 0; j < th; j++)
				for (uint i = 0; i < tw; i++, n++)
				{
					const uint x = tx + i, y = ty + j;
					uint v;
					if (bpp == 8)
						v = idx[n];
					else
					{
						const u8 b = idx[n >> 1];
						v = (n & 1) ? (b & 0x0f) : (b >> 4);
					}
					if (x >= w || y >= h || v >= pal_n)
						continue;
					const u8 *e = pal + v * 4;
					u8 *px = rgba + ((u64)y * w + x) * 4;
					px[0] = e[0]; // R
					px[1] = e[1]; // G
					px[2] = e[2]; // B
					px[3] = e[3]; // A
				}

	*rgba_out = rgba;
	*width_out = w;
	*height_out = h;
	return ERR_OK;

truncated:
	FREE (rgba);
	return ERR_INVALID_DATA;
}
