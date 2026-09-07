// Prosonic SDK (PSDK) compression -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

//-----------------------------------------------------------------------------
///////////////		Prosonic SDK (PSDK) Compression			///////////////
//-----------------------------------------------------------------------------

enumError DecodePSDK (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 8 || memcmp (src, "PSDK", 4))
		return EINVAL;

	const u32 uncomp_sz
		= (u32)src[4] | ((u32)src[5] << 8) | ((u32)src[6] << 16) | ((u32)src[7] << 24);
	if (!uncomp_sz || uncomp_sz > NFMT_MAX_OUTPUT)
		return EINVAL;

	u8 *out = MALLOC (uncomp_sz);
	if (!out)
		return ERR_CANT_CREATE;

	uint src_pos = 8;
	if (src_size >= 12 && src[8] == 0 && src[9] == 0 && src[10] == 0 && src[11] == 0)
		src_pos = 12;

	uint out_pos = 0;
	while (out_pos < uncomp_sz && src_pos < src_size)
	{
		const u8 flags = src[src_pos++];
		for (int b = 0; b < 8 && out_pos < uncomp_sz && src_pos < src_size; b++)
		{
			if (flags & (1 << b))
			{
				out[out_pos++] = src[src_pos++];
			}
			else
			{
				if (src_pos + 1 >= src_size)
					break;
				const u8 b1 = src[src_pos++];
				const u8 b2 = src[src_pos++];
				const uint disp = (((b2 & 0xF0) << 4) | b1) + 1;
				const uint len = (b2 & 0x0F) + 3;
				if (disp > out_pos)
					break;
				for (uint i = 0; i < len && out_pos < uncomp_sz; i++)
				{
					out[out_pos] = out[out_pos - disp];
					out_pos++;
				}
			}
		}
	}

	*dest = out;
	*dest_size = out_pos;
	return ERR_OK;
}

enumError EncodePSDK (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > NFMT_MAX_OUTPUT)
		return EINVAL;

	const uint n_chunks = (src_size + 7) / 8;
	const uint total_sz = 8 + src_size + n_chunks;
	u8 *out = MALLOC (total_sz);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, "PSDK", 4);
	out[4] = (u8)(src_size & 0xFF);
	out[5] = (u8)((src_size >> 8) & 0xFF);
	out[6] = (u8)((src_size >> 16) & 0xFF);
	out[7] = (u8)((src_size >> 24) & 0xFF);

	uint src_pos = 0;
	uint out_pos = 8;
	while (src_pos < src_size)
	{
		const uint take = src_size - src_pos > 8 ? 8 : src_size - src_pos;
		out[out_pos++] = 0xFF; // all literals
		for (uint i = 0; i < take; i++)
			out[out_pos++] = src[src_pos++];
	}

	*dest = out;
	*dest_size = out_pos;
	return ERR_OK;
}
