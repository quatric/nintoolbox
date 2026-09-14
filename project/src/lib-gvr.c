// Sega GameCube/Wii GVR texture wrapper. The texture body uses the GX
// tile layouts, but has its own tiny GCIX/GVRT header. This decoder
// covers the non-paletted formats used by Super Monkey Ball: Banana
// Blitz, independently verified against the MIT-licensed GvrTool project
// (https://github.com/MaikelChan/GvrTool), whose format notes credit the
// earlier Puyo Tools research.

#include "lib-std.h"
#include "lib-image.h"

// GVR is Sega's GameCube/Wii texture wrapper.  The texture body uses the GX
// tile layouts, but has its own tiny GCIX/GVRT header.  This decoder covers
// the non-paletted formats used by Super Monkey Ball: Banana Blitz.
//
// The layout was independently verified against the MIT-licensed GvrTool
// project (https://github.com/MaikelChan/GvrTool), whose format notes credit
// the earlier Puyo Tools research.
static inline u8 GVRScale (uint value, uint max)
{
	return (u8)(value * 255 / max);
}

static void GVRStore (u8 *rgba, uint width, uint height, uint x, uint y, u8 r, u8 g, u8 b, u8 a)
{
	if (x < width && y < height)
	{
		u8 *dest = rgba + ((size_t)y * width + x) * 4;
		dest[0] = r;
		dest[1] = g;
		dest[2] = b;
		dest[3] = a;
	}
}

static void GVRDecode565 (u16 pixel, u8 *rgba)
{
	rgba[0] = GVRScale (pixel >> 11, 31);
	rgba[1] = GVRScale (pixel >> 5 & 63, 63);
	rgba[2] = GVRScale (pixel & 31, 31);
	rgba[3] = 255;
}

enumError DecodeGVR_RGBA (
	u8 **rgba_ptr, uint *width_ptr, uint *height_ptr, const u8 *data, uint data_size)
{
	*rgba_ptr = 0;
	*width_ptr = *height_ptr = 0;
	if (data_size < 0x20 || memcmp (data, "GCIX", 4) || memcmp (data + 0x10, "GVRT", 4))
		return ERR_INVALID_DATA;

	const uint flags = data[0x1a] & 0x0f;
	const uint format = data[0x1b];
	const uint width = be_func.rd16 (data + 0x1c);
	const uint height = be_func.rd16 (data + 0x1e);
	if (!width || !height || width > 16384 || height > 16384
		|| (size_t)width * height > UINT_MAX / 4)
		return ERR_INVALID_DATA;
	if (flags & 0x0b) // mipmaps or palettes require a separately selected level/palette
		return ERR_INVALID_DATA;

	u8 *rgba = MALLOC ((size_t)width * height * 4);
	const u8 *src = data + 0x20, *end = data + data_size;
#define GVR_NEED(n)                                                                                \
	do                                                                                             \
	{                                                                                              \
		if ((size_t)(end - src) < (n))                                                             \
			goto invalid_gvr;                                                                      \
	} while (0)

	if (format == 0 || format == 1 || format == 2 || format == 3)
	{
		const uint tw = format == 0 ? 8 : format == 1 || format == 2 ? 8 : 4;
		const uint th = format == 0 ? 8 : format == 1 || format == 2 ? 4 : 4;
		for (uint by = 0; by < height; by += th)
			for (uint bx = 0; bx < width; bx += tw)
				for (uint y = 0; y < th; y++)
					for (uint x = 0; x < tw; x++)
					{
						u8 i, a = 255;
						if (format == 0)
						{
							GVR_NEED (1);
							i = x & 1 ? *src++ & 15 : *src >> 4;
						}
						else
						{
							GVR_NEED (format == 3 ? 2 : 1);
							i = *src++;
							if (format == 2)
							{
								a = GVRScale (i >> 4, 15);
								i = GVRScale (i & 15, 15);
							}
							else if (format == 3)
							{
								a = i;
								i = *src++;
							}
						}
						if (format == 0)
							i = GVRScale (i, 15);
						GVRStore (rgba, width, height, bx + x, by + y, i, i, i, a);
					}
	}
	else if (format == 4 || format == 5)
	{
		for (uint by = 0; by < height; by += 4)
			for (uint bx = 0; bx < width; bx += 4)
				for (uint y = 0; y < 4; y++)
					for (uint x = 0; x < 4; x++)
					{
						GVR_NEED (2);
						const u16 p = be_func.rd16 (src);
						src += 2;
						u8 out[4];
						if (format == 4)
							GVRDecode565 (p, out);
						else if (p & 0x8000)
						{
							out[0] = GVRScale (p >> 10 & 31, 31);
							out[1] = GVRScale (p >> 5 & 31, 31);
							out[2] = GVRScale (p & 31, 31);
							out[3] = 255;
						}
						else
						{
							out[0] = GVRScale (p >> 8 & 15, 15);
							out[1] = GVRScale (p >> 4 & 15, 15);
							out[2] = GVRScale (p & 15, 15);
							out[3] = GVRScale (p >> 12 & 7, 7);
						}
						GVRStore (
							rgba, width, height, bx + x, by + y, out[0], out[1], out[2], out[3]);
					}
	}
	else if (format == 6)
	{
		for (uint by = 0; by < height; by += 4)
			for (uint bx = 0; bx < width; bx += 4)
			{
				GVR_NEED (64);
				for (uint y = 0; y < 4; y++)
					for (uint x = 0; x < 4; x++)
					{
						const uint i = (y * 4 + x) * 2;
						GVRStore (rgba, width, height, bx + x, by + y, src[i + 1], src[32 + i],
							src[33 + i], src[i]);
					}
				src += 64;
			}
	}
	else if (format == 0x0e)
	{
		for (uint by = 0; by < height; by += 8)
			for (uint bx = 0; bx < width; bx += 8)
				for (uint sy = 0; sy < 8; sy += 4)
					for (uint sx = 0; sx < 8; sx += 4)
					{
						GVR_NEED (8);
						u8 pal[4][4];
						GVRDecode565 (be_func.rd16 (src), pal[0]);
						GVRDecode565 (be_func.rd16 (src + 2), pal[1]);
						const u16 p0 = be_func.rd16 (src), p1 = be_func.rd16 (src + 2);
						src += 4;
						for (uint c = 0; c < 3; c++)
							if (p0 > p1)
							{
								pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3;
								pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3;
							}
							else
							{
								pal[2][c] = (pal[0][c] + pal[1][c]) / 2;
								pal[3][c] = 0;
							}
						pal[2][3] = 255;
						pal[3][3] = p0 > p1 ? 255 : 0;
						for (uint y = 0; y < 4; y++)
						{
							const u8 row = *src++;
							for (uint x = 0; x < 4; x++)
							{
								const u8 *p = pal[row >> (6 - 2 * x) & 3];
								GVRStore (rgba, width, height, bx + sx + x, by + sy + y, p[0], p[1],
									p[2], p[3]);
							}
						}
					}
	}
	else
		goto invalid_gvr;

#undef GVR_NEED
	*rgba_ptr = rgba;
	*width_ptr = width;
	*height_ptr = height;
	return ERR_OK;

invalid_gvr:
#undef GVR_NEED
	FREE (rgba);
	return ERR_INVALID_DATA;
}

