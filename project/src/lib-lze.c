// LZE (Luminous Arc 2/3) token layout documented by CUE's lze.c (2011).
// Bounded implementation; see docs/FORMAT_AUDIT.md for reference and validation.
#include "lib-std.h"
#include "lib-nintendo.h"

static bool walk_lze (u8 *out, const u8 *src, uint size, uint length)
{
	uint sp = 6, dp = 0;
	while (dp < length)
	{
		if (sp >= size)
			return false;
		uint flags = src[sp++];
		for (uint token = 0; token < 4 && dp < length; token++, flags >>= 2)
		{
			const uint mode = flags & 3;
			uint count, back = 0;
			if (mode < 2)
			{
				if (sp >= size || (!mode && size - sp < 2))
					return false;
				uint value = src[sp++];
				if (!mode)
				{
					value |= (uint)src[sp++] << 8;
					count = (value >> 12) + 3;
					back = (value & 0xfff) + 5;
				}
				else
				{
					count = (value >> 2) + 2;
					back = (value & 3) + 1;
				}
				if (back > dp || count > length - dp)
					return false;
				if (out)
					for (uint i = 0; i < count; i++)
						out[dp + i] = out[dp + i - back];
			}
			else
			{
				count = mode == 2 ? 1 : 3;
				// The final three-literal token may end at the declared size.
				if (count > length - dp)
					count = length - dp;
				if (count > size - sp)
					return false;
				if (out)
					memcpy (out + dp, src + sp, count);
				sp += count;
			}
			dp += count;
		}
	}
	return true;
}

enumError DecodeLZE (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size)
		return ERR_INVALID_DATA;
	*dest = 0;
	*dest_size = 0;
	if (!src || src_size < 6 || memcmp (src, "Le", 2))
		return ERR_INVALID_DATA;
	const uint length = rd_le32 (src + 2);
	if (length > NFMT_MAX_OUTPUT || !walk_lze (0, src, src_size, length))
		return ERR_INVALID_DATA;
	if (!length)
		return ERR_OK;
	enumError err = AllocOutput (dest, dest_size, length);
	if (err)
		return err;
	walk_lze (*dest, src, src_size, length);
	return ERR_OK;
}
