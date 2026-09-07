// Namco Museum SSZL LZSS0 compression -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

//-----------------------------------------------------------------------------
///////////////		Namco Museum SSZL LZSS0 Compression		///////////////
//-----------------------------------------------------------------------------

enumError DecodeSSZL (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 16 || memcmp (src, "SSZL", 4))
		return EINVAL;

	const u32 zsize
		= (u32)src[8] | ((u32)src[9] << 8) | ((u32)src[10] << 16) | ((u32)src[11] << 24);
	const u32 usize
		= (u32)src[12] | ((u32)src[13] << 8) | ((u32)src[14] << 16) | ((u32)src[15] << 24);

	if (!usize || usize > NFMT_MAX_OUTPUT || 16 + zsize > src_size)
		return EINVAL;

	u8 *out = MALLOC (usize);
	if (!out)
		return ERR_CANT_CREATE;

	u8 ring[4096];
	memset (ring, 0, sizeof (ring));
	uint r = 4096 - 18, ip = 0, op = 0;
	uint flags = 0;
	const u8 *in = src + 16;

	while (op < usize)
	{
		if (!(flags & 0x100))
		{
			if (ip >= zsize)
			{
				FREE (out);
				return EINVAL;
			}
			flags = in[ip++] | 0xff00;
		}
		if (flags & 1)
		{
			if (ip >= zsize)
			{
				FREE (out);
				return EINVAL;
			}
			out[op++] = ring[r] = in[ip++];
			r = (r + 1) & 0xfff;
		}
		else
		{
			if (ip + 1 >= zsize)
			{
				FREE (out);
				return EINVAL;
			}
			uint p = in[ip++];
			const uint b = in[ip++];
			p |= (b & 0xf0) << 4;
			uint n = (b & 0x0f) + 3;
			if (n > usize - op)
			{
				FREE (out);
				return EINVAL;
			}
			while (n--)
			{
				out[op++] = ring[r] = ring[p++ & 0xfff];
				r = (r + 1) & 0xfff;
			}
		}
		flags >>= 1;
	}

	*dest = out;
	*dest_size = usize;
	return ERR_OK;
}

enumError EncodeSSZL (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > NFMT_MAX_OUTPUT)
		return EINVAL;

	const uint n_chunks = (src_size + 7) / 8;
	const uint total_sz = 16 + src_size + n_chunks;
	u8 *out = MALLOC (total_sz);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, "SSZL", 4);
	out[4] = out[5] = out[6] = out[7] = 0;

	uint src_pos = 0;
	uint out_pos = 16;
	while (src_pos < src_size)
	{
		const uint take = src_size - src_pos > 8 ? 8 : src_size - src_pos;
		out[out_pos++] = 0xFF; // 8 literal bits
		for (uint i = 0; i < take; i++)
			out[out_pos++] = src[src_pos++];
	}

	const u32 zsize = out_pos - 16;
	out[8] = (u8)(zsize & 0xFF);
	out[9] = (u8)((zsize >> 8) & 0xFF);
	out[10] = (u8)((zsize >> 16) & 0xFF);
	out[11] = (u8)((zsize >> 24) & 0xFF);

	out[12] = (u8)(src_size & 0xFF);
	out[13] = (u8)((src_size >> 8) & 0xFF);
	out[14] = (u8)((src_size >> 16) & 0xFF);
	out[15] = (u8)((src_size >> 24) & 0xFF);

	*dest = out;
	*dest_size = out_pos;
	return ERR_OK;
}
