// LZX compression (0x19 Extended LZ11) -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

//-----------------------------------------------------------------------------
///////////////		LZX Compression (0x19 Extended LZ11)		///////////////
//-----------------------------------------------------------------------------

int CxIsCompressedLZX (const unsigned char *buffer, unsigned int size)
{
	if (!buffer || size < 4 || buffer[0] != 0x19)
		return 0;
	const uint uncomp_size = ((uint)buffer[1]) | ((uint)buffer[2] << 8) | ((uint)buffer[3] << 16);
	return (uncomp_size > 0 && uncomp_size <= NFMT_MAX_OUTPUT);
}

enumError DecodeLZX (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !CxIsCompressedLZX (src, src_size))
		return EINVAL;

	const uint uncomp_size = ((uint)src[1]) | ((uint)src[2] << 8) | ((uint)src[3] << 16);
	enumError err = AllocOutput (dest, dest_size, uncomp_size);
	if (err)
		return err;

	u8 *out = *dest;
	uint offset = 4, dst_offset = 0;

	while (offset < src_size && dst_offset < uncomp_size)
	{
		u8 head = src[offset++];
		for (int i = 0; i < 8 && dst_offset < uncomp_size; i++)
		{
			const bool is_ref = (head & 0x80) != 0;
			head <<= 1;

			if (!is_ref)
			{
				if (offset >= src_size)
					goto fail_lzx;
				out[dst_offset++] = src[offset++];
			}
			else
			{
				if (offset + 2 > src_size)
					goto fail_lzx;
				const u8 high = src[offset++];
				const u8 low = src[offset++];
				const uint mode = high >> 4;
				uint len = 0, offs = 0;

				if (mode == 0)
				{
					if (offset >= src_size)
						goto fail_lzx;
					const u8 low2 = src[offset++];
					len = ((high << 4) | (low >> 4)) + 0x11;
					offs = (((low & 0xF) << 8) | low2) + 1;
				}
				else if (mode == 1)
				{
					if (offset + 2 > src_size)
						goto fail_lzx;
					const u8 low2 = src[offset++];
					const u8 low3 = src[offset++];
					len = (((high & 0xF) << 12) | (low << 4) | (low2 >> 4)) + 0x111;
					offs = (((low2 & 0xF) << 8) | low3) + 1;
				}
				else
				{
					len = (high >> 4) + 1;
					offs = (((high & 0xF) << 8) | low) + 1;
				}

				if (offs > dst_offset || len > uncomp_size - dst_offset)
					goto fail_lzx;

				for (uint j = 0; j < len; j++)
				{
					out[dst_offset] = out[dst_offset - offs];
					dst_offset++;
				}
			}
		}
	}

	if (dst_offset == uncomp_size)
		return ERR_OK;
fail_lzx:
	FREE (*dest);
	*dest = 0;
	*dest_size = 0;
	return EINVAL;
}

enumError EncodeLZX (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > 0x00ffffff)
		return EINVAL;

	// Use short/long token encoder similar to LZ11 with 0x19 header
	const uint max_out = 4 + src_size + (src_size / 8 + 1) * 2 + 16;
	u8 *out = CALLOC (1, max_out);
	if (!out)
		return ERR_CANT_CREATE;

	out[0] = 0x19;
	out[1] = (u8)(src_size & 0xFF);
	out[2] = (u8)((src_size >> 8) & 0xFF);
	out[3] = (u8)((src_size >> 16) & 0xFF);

	uint srcpos = 0, dstpos = 4;
	while (srcpos < src_size)
	{
		u8 *flag_byte = out + dstpos++;
		*flag_byte = 0;

		for (int bit = 7; bit >= 0 && srcpos < src_size; bit--)
		{
			// Search for LZ match
			uint best_len = 0, best_dist = 0;
			const uint max_dist = srcpos < 4096 ? srcpos : 4096;
			const uint max_len
				= src_size - srcpos < 0xFFFF + 0x111 ? src_size - srcpos : 0xFFFF + 0x111;

			if (max_len >= 3)
			{
				for (uint d = 1; d <= max_dist; d++)
				{
					uint l = 0;
					while (l < max_len && src[srcpos + l] == src[srcpos - d + l])
						l++;
					if (l > best_len && l >= 3)
					{
						best_len = l;
						best_dist = d;
						if (best_len >= 256)
							break;
					}
				}
			}

			if (best_len >= 3)
			{
				*flag_byte |= (1 << bit);
				const uint d = best_dist - 1;
				if (best_len <= 16)
				{
					// Mode 2..15: 4-bit length - 1, 12-bit offset
					const u8 hi = (u8)(((best_len - 1) << 4) | ((d >> 8) & 0xF));
					const u8 lo = (u8)(d & 0xFF);
					out[dstpos++] = hi;
					out[dstpos++] = lo;
				}
				else if (best_len <= 0xFF + 0x11)
				{
					// Mode 0: 8-bit length - 0x11, 12-bit offset
					const uint adj = best_len - 0x11;
					out[dstpos++] = (u8)(adj >> 4);
					out[dstpos++] = (u8)(((adj & 0xF) << 4) | ((d >> 8) & 0xF));
					out[dstpos++] = (u8)(d & 0xFF);
				}
				else
				{
					// Mode 1: 16-bit length - 0x111, 12-bit offset
					const uint adj = best_len - 0x111;
					out[dstpos++] = (u8)(0x10 | ((adj >> 12) & 0xF));
					out[dstpos++] = (u8)((adj >> 4) & 0xFF);
					out[dstpos++] = (u8)(((adj & 0xF) << 4) | ((d >> 8) & 0xF));
					out[dstpos++] = (u8)(d & 0xFF);
				}
				srcpos += best_len;
			}
			else
			{
				out[dstpos++] = src[srcpos++];
			}
		}
	}

	*dest = out;
	*dest_size = dstpos;
	return ERR_OK;
}
