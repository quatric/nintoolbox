// Differential filter (0x80 / 0x81) -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

//-----------------------------------------------------------------------------
///////////////		Differential Filter (0x80 / 0x81)			///////////////
//-----------------------------------------------------------------------------

enumError DecodeDiff8 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 4 || src[0] != 0x80)
		return EINVAL;
	const uint uncomp_size = ((uint)src[1]) | ((uint)src[2] << 8) | ((uint)src[3] << 16);
	if (!uncomp_size || uncomp_size > NFMT_MAX_OUTPUT)
		return EINVAL;

	enumError err = AllocOutput (dest, dest_size, uncomp_size);
	if (err)
		return err;

	u8 *out = *dest;
	u8 prev = 0;
	for (uint i = 0; i < uncomp_size && 4 + i < src_size; i++)
	{
		prev += src[4 + i];
		out[i] = prev;
	}
	return ERR_OK;
}

enumError EncodeDiff8 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > 0x00ffffff)
		return EINVAL;

	u8 *out = CALLOC (1, 4 + src_size);
	if (!out)
		return ERR_CANT_CREATE;

	out[0] = 0x80;
	out[1] = (u8)(src_size & 0xFF);
	out[2] = (u8)((src_size >> 8) & 0xFF);
	out[3] = (u8)((src_size >> 16) & 0xFF);

	u8 prev = 0;
	for (uint i = 0; i < src_size; i++)
	{
		out[4 + i] = src[i] - prev;
		prev = src[i];
	}

	*dest = out;
	*dest_size = 4 + src_size;
	return ERR_OK;
}

enumError DecodeDiff16 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 4 || src[0] != 0x81)
		return EINVAL;
	const uint uncomp_size = ((uint)src[1]) | ((uint)src[2] << 8) | ((uint)src[3] << 16);
	if (!uncomp_size || uncomp_size > NFMT_MAX_OUTPUT)
		return EINVAL;

	enumError err = AllocOutput (dest, dest_size, uncomp_size);
	if (err)
		return err;

	u8 *out = *dest;
	u16 prev = 0;
	for (uint i = 0; i + 1 < uncomp_size && 4 + i + 1 < src_size; i += 2)
	{
		const u16 diff = (u16)src[4 + i] | ((u16)src[4 + i + 1] << 8);
		prev += diff;
		out[i] = (u8)(prev & 0xFF);
		out[i + 1] = (u8)(prev >> 8);
	}
	return ERR_OK;
}

enumError EncodeDiff16 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > 0x00ffffff)
		return EINVAL;

	const uint out_sz = ((src_size + 1) & ~1u) + 4;
	u8 *out = CALLOC (1, out_sz);
	if (!out)
		return ERR_CANT_CREATE;

	out[0] = 0x81;
	out[1] = (u8)(src_size & 0xFF);
	out[2] = (u8)((src_size >> 8) & 0xFF);
	out[3] = (u8)((src_size >> 16) & 0xFF);

	u16 prev = 0;
	for (uint i = 0; i < src_size; i += 2)
	{
		const u16 val = (i + 1 < src_size) ? ((u16)src[i] | ((u16)src[i + 1] << 8)) : (u16)src[i];
		const u16 diff = val - prev;
		out[4 + i] = (u8)(diff & 0xFF);
		out[4 + i + 1] = (u8)(diff >> 8);
		prev = val;
	}

	*dest = out;
	*dest_size = out_sz;
	return ERR_OK;
}
