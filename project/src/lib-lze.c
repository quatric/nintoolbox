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

// Greedy encoder: mode 1 (back 1-4, 2-65 bytes), mode 0 (back 5-4100, 3-18
// bytes), literals as mode 3 (three bytes) / mode 2 (one byte).
// shortcut: brute-force window scan, fine for the small Luminous Arc resources; upgrade to a hash chain if large inputs matter.
enumError EncodeLZE (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || (src_size && !src) || src_size > NFMT_MAX_OUTPUT)
		return ERR_INVALID_DATA;
	u8 *out = MALLOC (6 + (size_t)src_size * 2 + 16);
	if (!out)
		return ERR_OUT_OF_MEMORY;
	memcpy (out, "Le", 2);
	out[2] = src_size, out[3] = src_size >> 8, out[4] = src_size >> 16, out[5] = src_size >> 24;
	uint dp = 6, flag_pos = 0, ntok = 0, sp = 0, lit = 0;
#define LZE_TOKEN(mode)                  \
	do                                   \
	{                                    \
		if (!(ntok & 3))                 \
		{                                \
			flag_pos = dp++;             \
			out[flag_pos] = 0;           \
		}                                \
		out[flag_pos] |= (mode) << ((ntok & 3) * 2); \
		ntok++;                          \
	} while (0)
#define LZE_FLUSH_LITERALS(end)                                                   \
	while (lit < (end))                                                           \
	{                                                                             \
		const uint n = (end) - lit >= 3 ? 3 : 1;                                  \
		LZE_TOKEN (n == 3 ? 3 : 2);                                               \
		memcpy (out + dp, src + lit, n);                                          \
		dp += n, lit += n;                                                        \
	}
	while (sp < src_size)
	{
		uint best = 0, best_back = 0;
		const uint max_back = sp < 4100 ? sp : 4100;
		for (uint back = 1; back <= max_back; back++)
		{
			const uint cap = back <= 4 ? 65 : 18;
			uint n = 0;
			while (n < cap && sp + n < src_size && src[sp + n] == src[sp + n - back])
				n++;
			if (n > best && n >= (back <= 4 ? 2 : 3))
				best = n, best_back = back;
		}
		if (!best)
		{
			sp++;
			continue;
		}
		LZE_FLUSH_LITERALS (sp);
		if (best_back <= 4)
		{
			LZE_TOKEN (1);
			out[dp++] = (best - 2) << 2 | (best_back - 1);
		}
		else
		{
			LZE_TOKEN (0);
			const uint v = (best - 3) << 12 | (best_back - 5);
			out[dp++] = v, out[dp++] = v >> 8;
		}
		sp += best;
		lit = sp;
	}
	LZE_FLUSH_LITERALS (src_size);
#undef LZE_FLUSH_LITERALS
#undef LZE_TOKEN
	*dest = out;
	*dest_size = dp;
	return ERR_OK;
}
