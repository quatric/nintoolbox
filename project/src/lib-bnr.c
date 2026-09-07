// GameCube/Wii BNR banner format -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

enumError DecodeBNR_RGBA (u8 **dest, const u8 *src, uint src_size)
{
	if (!dest || !src || src_size < 0x20 + 96 * 32 * 2
		|| (memcmp (src, "BNR1", 4) && memcmp (src, "BNR2", 4)))
		return EINVAL;
	u8 *rgba = MALLOC (96 * 32 * 4);
	if (!rgba)
		return ERR_CANT_CREATE;
	const u8 *pixels = src + 0x20;
	for (uint by = 0; by < 32 / 4; by++)
		for (uint bx = 0; bx < 96 / 4; bx++)
			for (uint y = 0; y < 4; y++)
				for (uint x = 0; x < 4; x++)
				{
					const uint pi = 16 * (by * (96 / 4) + bx) + 4 * y + x;
					const u16 c = rd_be16 (pixels + 2 * pi);
					u8 *d = rgba + 4 * ((4 * by + y) * 96 + 4 * bx + x);
					if (c & 0x8000)
					{
						d[0] = expand5 (c >> 10 & 31);
						d[1] = expand5 (c >> 5 & 31);
						d[2] = expand5 (c & 31);
						d[3] = 255;
					}
					else
					{
						d[0] = (c >> 8 & 15) * 17;
						d[1] = (c >> 4 & 15) * 17;
						d[2] = (c & 15) * 17;
						d[3] = (c >> 12 & 7) * 255 / 7;
					}
				}
	*dest = rgba;
	return ERR_OK;
}

enumError EncodeBNR_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height)
{
	if (!dest || !dest_size || !rgba || width != 96 || height != 32)
		return EINVAL;
	// BNR1 is 0x1960 bytes: 0x20-byte header, 96x32 RGB5A3 icon, and six
	// zero-filled Shift-JIS title fields.  Empty fields are legal and make a
	// useful canonical banner when the input is a PNG rather than a BNR file.
	const uint size = 0x1960;
	u8 *out = CALLOC (1, size);
	if (!out)
		return ERR_CANT_CREATE;
	memcpy (out, "BNR1", 4);
	u8 *pixels = out + 0x20;
	for (uint by = 0; by < 32 / 4; by++)
		for (uint bx = 0; bx < 96 / 4; bx++)
			for (uint y = 0; y < 4; y++)
				for (uint x = 0; x < 4; x++)
				{
					const u8 *s = rgba + 4 * ((4 * by + y) * 96 + 4 * bx + x);
					u16 c;
					if (s[3] >= 224)
						c = 0x8000 | (u16)(s[0] >> 3) << 10 | (u16)(s[1] >> 3) << 5 | (s[2] >> 3);
					else
						c = (u16)((s[3] * 7 + 127) / 255) << 12 | (u16)(s[0] >> 4) << 8
							| (u16)(s[1] >> 4) << 4 | (s[2] >> 4);
					const uint pi = 16 * (by * (96 / 4) + bx) + 4 * y + x;
					wr_be16 (pixels + 2 * pi, c);
				}
	*dest = out;
	*dest_size = size;
	return ERR_OK;
}
