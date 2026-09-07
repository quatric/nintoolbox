// Jump Ultimate Stars ALAR archive -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

//-----------------------------------------------------------------------------
///////////////		Jump Ultimate Stars Archive (ALAR)		///////////////
//-----------------------------------------------------------------------------

enumError DecodeALAR (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 16 || memcmp (src, "ALAR", 4))
		return EINVAL;

	const u8 type = src[4];
	if (type == 2)
	{
		const uint num_files = (uint)src[6] | ((uint)src[7] << 8);
		if (!num_files || 16 + num_files * 16 > src_size)
			return EINVAL;
		const uint ofs0 = (uint)src[16 + 4] | ((uint)src[16 + 5] << 8) | ((uint)src[16 + 6] << 16)
			| ((uint)src[16 + 7] << 24);
		const uint sz0 = (uint)src[16 + 8] | ((uint)src[16 + 9] << 8) | ((uint)src[16 + 10] << 16)
			| ((uint)src[16 + 11] << 24);
		if (ofs0 + sz0 <= src_size && sz0 > 0)
		{
			u8 *out = MALLOC (sz0);
			if (!out)
				return ERR_CANT_CREATE;
			memcpy (out, src + ofs0, sz0);
			*dest = out;
			*dest_size = sz0;
			return ERR_OK;
		}
	}
	else if (type == 3)
	{
		const uint num_files
			= (uint)src[6] | ((uint)src[7] << 8) | ((uint)src[8] << 16) | ((uint)src[9] << 24);
		if (!num_files || src_size < 32)
			return EINVAL;
		const uint ofs0 = (uint)src[16 + 4] | ((uint)src[16 + 5] << 8) | ((uint)src[16 + 6] << 16)
			| ((uint)src[16 + 7] << 24);
		const uint sz0 = (uint)src[16 + 8] | ((uint)src[16 + 9] << 8) | ((uint)src[16 + 10] << 16)
			| ((uint)src[16 + 11] << 24);
		if (ofs0 + sz0 <= src_size && sz0 > 0)
		{
			u8 *out = MALLOC (sz0);
			if (!out)
				return ERR_CANT_CREATE;
			memcpy (out, src + ofs0, sz0);
			*dest = out;
			*dest_size = sz0;
			return ERR_OK;
		}
	}
	return EINVAL;
}
