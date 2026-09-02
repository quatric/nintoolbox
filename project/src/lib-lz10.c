#include "lib-std.h"
#include "lib-lz10.h"
#include <string.h>
#include <errno.h>
#include <limits.h>

enumError DecodeLZ10LZ11 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!src || src_size < 4 || (src[0] != 0x10 && src[0] != 0x11))
		return EINVAL;
	const bool lz11 = src[0] == 0x11;
	const u32 out_len = (u32)src[1] | (u32)src[2] << 8 | (u32)src[3] << 16;
	enumError err = AllocOutput (dest, dest_size, out_len);
	if (err)
		return err;
	uint sp = 4, dp = 0;
	while (dp < out_len)
	{
		if (sp >= src_size)
			goto invalid;
		u8 flags = src[sp++];
		for (uint bit = 0; bit < 8 && dp < out_len; bit++, flags <<= 1)
			if (!(flags & 0x80))
			{
				if (sp >= src_size)
					goto invalid;
				(*dest)[dp++] = src[sp++];
			}
			else
			{
				if (sp + 2 > src_size)
					goto invalid;
				u8 a = src[sp++], b = src[sp++];
				uint len, back;
				if (!lz11)
				{
					len = (a >> 4) + 3;
					back = ((a & 15) << 8 | b) + 1;
				}
				else if (a >> 4 == 0)
				{
					if (sp >= src_size)
						goto invalid;
					len = ((a & 15) << 4 | b >> 4) + 0x11;
					back = ((b & 15) << 8 | src[sp++]) + 1;
				}
				else if (a >> 4 == 1)
				{
					if (sp + 2 > src_size)
						goto invalid;
					len = ((a & 15) << 12 | b << 4 | src[sp] >> 4) + 0x111;
					const u8 c = src[sp++], d = src[sp++];
					back = ((c & 15) << 8 | d) + 1;
				}
				else
				{
					len = (a >> 4) + 1;
					back = ((a & 15) << 8 | b) + 1;
				}
				if (back > dp || len > out_len - dp)
					goto invalid;
				while (len--)
					(*dest)[dp] = (*dest)[dp - back], dp++;
			}
	}
	return ERR_OK;
invalid:
	FREE (*dest);
	*dest = 0;
	*dest_size = 0;
	return EINVAL;
}

enumError EncodeLZ10LZ11 (u8 **dest, uint *dest_size, const u8 *src, uint src_size, bool lz11)
{
	if (!dest || !dest_size || !src || !src_size || src_size > 0xffffff)
		return EINVAL;

	// Worst case is one flag byte per eight literals plus the four-byte
	// header.  A little extra also covers the final, partial group.
	const uint capacity = 4 + src_size + (src_size + 7) / 8;
	u8 *out = MALLOC (capacity);
	if (!out)
		return ERR_CANT_CREATE;
	out[0] = lz11 ? 0x11 : 0x10;
	out[1] = src_size;
	out[2] = src_size >> 8;
	out[3] = src_size >> 16;

	uint sp = 0, dp = 4;
	while (sp < src_size)
	{
		const uint flags_pos = dp++;
		u8 flags = 0;
		for (uint bit = 0; bit < 8 && sp < src_size; bit++)
		{
			uint best_len = 0, best_back = 0;
			const uint max_back = sp < 0x1000 ? sp : 0x1000;
			const uint max_len = lz11 ? (src_size - sp < 16 ? src_size - sp : 16)
									  : (src_size - sp < 18 ? src_size - sp : 18);
			// A backwards search is deliberately used: nearby matches tend
			// to give the same compact stream as Nintendo's common tools.
			for (uint back = 1; back <= max_back; back++)
			{
				uint len = 0;
				while (len < max_len && src[sp + len] == src[sp - back + len])
					len++;
				if (len > best_len)
				{
					best_len = len;
					best_back = back;
					if (len == max_len)
						break;
				}
			}
			if (best_len >= 3)
			{
				flags |= 0x80 >> bit;
				const uint disp = best_back - 1;
				if (lz11)
				{
					// The regular LZ11 token represents lengths 3..16.
					out[dp++] = (best_len - 1) << 4 | (disp >> 8);
					out[dp++] = disp;
				}
				else
				{
					out[dp++] = (best_len - 3) << 4 | (disp >> 8);
					out[dp++] = disp;
				}
				sp += best_len;
			}
			else
				out[dp++] = src[sp++];
		}
		out[flags_pos] = flags;
	}
	*dest = out;
	*dest_size = dp;
	return ERR_OK;
}

enumError EncodeLZ10Raw (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	u8 *lz10 = 0;
	uint lz10_size = 0;
	enumError err = EncodeLZ10LZ11 (&lz10, &lz10_size, src, src_size, false);
	if (!err)
	{
		if (lz10_size > 4)
		{
			*dest_size = lz10_size - 4;
			*dest = MALLOC (*dest_size);
			if (*dest)
				memcpy (*dest, lz10 + 4, *dest_size);
			else
				err = ERR_CANT_CREATE;
		}
		else
			err = ERR_INVALID_DATA;
		FREE (lz10);
	}
	return err;
}

