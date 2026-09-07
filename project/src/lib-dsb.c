// Nintendo DSB image format -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

enumError DecodeDSB_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size)
{
	if (!dest || !width || !height || !src || src_size <= 0x60 || memcmp (src, "TXTR", 4))
		return EINVAL;

	// AC:WW's menu TXTR variant stores 32 little-endian RGB555 entries at
	// 0x20 and one A3I5 byte per pixel at 0x60.  The payload is square.
	const uint pixel_count = src_size - 0x60;
	uint side = 1;
	while (side <= pixel_count / side && side * side < pixel_count)
		side++;
	if (side * side != pixel_count || side > 1024)
		return EINVAL;

	u8 *rgba = MALLOC (pixel_count * 4);
	if (!rgba)
		return ERR_CANT_CREATE;

	const u8 *texel = src + 0x60;
	for (uint i = 0; i < pixel_count; i++)
	{
		const u8 value = texel[i];
		const uint poff = 0x20 + (value & 0x1f) * 2;
		const u16 color = src[poff] | (u16)src[poff + 1] << 8;
		rgba[4 * i + 0] = expand5 (color & 0x1f);
		rgba[4 * i + 1] = expand5 (color >> 5 & 0x1f);
		rgba[4 * i + 2] = expand5 (color >> 10 & 0x1f);
		rgba[4 * i + 3] = (value >> 5) * 255 / 7;
	}

	*dest = rgba;
	*width = *height = side;
	return ERR_OK;
}

enumError EncodeDSB_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height)
{
	if (!dest || !dest_size || !rgba || width != 128 || height != 128)
		return EINVAL;

	const uint pixels = width * height;
	const uint total = 0x60 + pixels;
	u8 *out = CALLOC (1, total);
	if (!out)
		return ERR_CANT_CREATE;

	static const u8 header[0x20] = { 'T', 'X', 'T', 'R', 0x10, 0x44, 0x60, 0x00, 0x60, 0x00, 0x10,
		0x20, 0x00, 0x01, 0x60, 0x00 };
	memcpy (out, header, sizeof (header));

	u16 palette[32] = { 0 };
	uint n_pal = 1;
	for (uint px = 0; px < pixels && n_pal < 32; px++)
	{
		const u8 *p = rgba + 4 * px;
		const u16 c = (u16)(p[0] >> 3) | (u16)(p[1] >> 3) << 5 | (u16)(p[2] >> 3) << 10;
		uint pi;
		for (pi = 0; pi < n_pal && palette[pi] != c; pi++)
		{
		}
		if (pi == n_pal)
			palette[n_pal++] = c;
	}
	for (uint pi = 0; pi < 32; pi++)
	{
		out[0x20 + 2 * pi] = palette[pi];
		out[0x21 + 2 * pi] = palette[pi] >> 8;
	}
	for (uint px = 0; px < pixels; px++)
	{
		const u8 *p = rgba + 4 * px;
		const int r = p[0] >> 3, g = p[1] >> 3, b = p[2] >> 3;
		uint best = 0, best_dist = UINT_MAX;
		for (uint pi = 0; pi < n_pal; pi++)
		{
			const int dr = r - (palette[pi] & 31);
			const int dg = g - (palette[pi] >> 5 & 31);
			const int db = b - (palette[pi] >> 10 & 31);
			const uint dist = dr * dr + dg * dg + db * db;
			if (dist < best_dist)
			{
				best = pi;
				best_dist = dist;
			}
		}
		out[0x60 + px] = ((p[3] * 7 + 127) / 255) << 5 | best;
	}
	*dest = out;
	*dest_size = total;
	return ERR_OK;
}
