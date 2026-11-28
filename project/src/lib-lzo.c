#include "lib-std.h"
#include "lib-lzo.h"
#include <string.h>
#include <limits.h>

// Clean-room LZO1X decoder. This follows the byte layout described in the
// Linux kernel's Documentation/staging/lzo.rst, rather than using (or
// translating) liblzo. In particular, it accepts only the original stream
// version used by Retro's PAKs; the newer LZO-RLE zero-run extension is not
// part of that format and is rejected here.
enumError DecodeLZO1XGrow (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size)
		return ERR_INVALID_DATA;

	uint cap = src_size <= ((256u << 20) - 256) / 4 ? src_size * 4 + 256 : 256u << 20;
	u8 *out = MALLOC (cap);
	if (!out)
		return ERR_OUT_OF_MEMORY;
	uint ip = 0, op = 0, state = 0;

	// Grow before every copy. The hard ceiling makes malformed streams unable
	// to turn a tiny CMPD segment into an unbounded allocation.
#define LZO_NEED_OUT(n)                                                                            \
	do                                                                                             \
	{                                                                                              \
		const uint lzo_need_ = (n);                                                                \
		if (lzo_need_ > (256u << 20) - op)                                                         \
			goto bad;                                                                              \
		const uint lzo_want_ = op + lzo_need_;                                                     \
		if (lzo_want_ > cap)                                                                       \
		{                                                                                          \
			uint lzo_cap_ = cap;                                                                   \
			while (lzo_cap_ < lzo_want_)                                                           \
			{                                                                                      \
				if (lzo_cap_ >= (256u << 20) / 2)                                                  \
				{                                                                                  \
					lzo_cap_ = 256u << 20;                                                         \
					break;                                                                         \
				}                                                                                  \
				lzo_cap_ *= 2;                                                                     \
			}                                                                                      \
			u8 *lzo_out_ = REALLOC (out, lzo_cap_);                                                \
			if (!lzo_out_)                                                                         \
				goto oom;                                                                          \
			out = lzo_out_;                                                                        \
			cap = lzo_cap_;                                                                        \
		}                                                                                          \
	} while (0)
#define LZO_COPY_LITERALS(n)                                                                       \
	do                                                                                             \
	{                                                                                              \
		const uint lzo_n_ = (n);                                                                   \
		if (lzo_n_ > src_size - ip)                                                                \
			goto bad;                                                                              \
		LZO_NEED_OUT (lzo_n_);                                                                     \
		memcpy (out + op, src + ip, lzo_n_);                                                       \
		ip += lzo_n_;                                                                              \
		op += lzo_n_;                                                                              \
	} while (0)

	// Returns an LZO variable length whose low bits were supplied by token.
	// Zero is the escape value: 15/7/31 respectively plus zero-byte steps.
#define LZO_LENGTH(bits, base, value, result)                                                      \
	do                                                                                             \
	{                                                                                              \
		uint lzo_v_ = (value);                                                                     \
		const uint lzo_mask_ = (1u << (bits)) - 1;                                                 \
		if (!lzo_v_)                                                                               \
		{                                                                                          \
			lzo_v_ = lzo_mask_;                                                                    \
			while (ip < src_size && src[ip] == 0)                                                  \
			{                                                                                      \
				if (lzo_v_ > UINT_MAX - 255)                                                       \
					goto bad;                                                                      \
				lzo_v_ += 255;                                                                     \
				ip++;                                                                              \
			}                                                                                      \
			if (ip >= src_size)                                                                    \
				goto bad;                                                                          \
			lzo_v_ += src[ip++];                                                                   \
		}                                                                                          \
		if (lzo_v_ > UINT_MAX - (base))                                                            \
			goto bad;                                                                              \
		(result) = lzo_v_ + (base);                                                                \
	} while (0)

	uint token = src[ip++];
	bool have_token = true;
	if (token > 17)
	{
		const uint n = token - 17;
		LZO_COPY_LITERALS (n);
		state = n < 4 ? n : 4;
		have_token = false;
	}

	for (;;)
	{
		if (!have_token)
		{
			if (ip >= src_size)
				goto bad;
			token = src[ip++];
		}
		have_token = false;
		uint len, dist;

		if (token < 16)
		{
			if (!state)
			{
				LZO_LENGTH (4, 3, token, len);
				LZO_COPY_LITERALS (len);
				state = 4;
				continue;
			}
			if (ip >= src_size)
				goto bad;
			len = state < 4 ? 2 : 3;
			dist = ((uint)src[ip++] << 2) + (token >> 2) + (state < 4 ? 1 : 2049);
			state = token & 3;
		}
		else if (token < 32)
		{
			LZO_LENGTH (3, 2, token & 7, len);
			if (src_size - ip < 2)
				goto bad;
			const uint d = src[ip] | (uint)src[ip + 1] << 8;
			ip += 2;
			dist = 16384 + ((token & 8) << 11) + (d >> 2);
			state = d & 3;
			if (dist == 16384)
			{
				if (ip != src_size)
					goto bad; // a segment is exactly one LZO stream
				*dest = out;
				*dest_size = op;
				return ERR_OK;
			}
		}
		else if (token < 64)
		{
			LZO_LENGTH (5, 2, token & 31, len);
			if (src_size - ip < 2)
				goto bad;
			const uint d = src[ip] | (uint)src[ip + 1] << 8;
			ip += 2;
			dist = (d >> 2) + 1;
			state = d & 3;
		}
		else if (token < 128)
		{
			if (ip >= src_size)
				goto bad;
			len = 3 + ((token >> 5) & 1);
			dist = ((uint)src[ip++] << 3) + ((token >> 2) & 7) + 1;
			state = token & 3;
		}
		else
		{
			if (ip >= src_size)
				goto bad;
			len = 5 + ((token >> 5) & 3);
			dist = ((uint)src[ip++] << 3) + ((token >> 2) & 7) + 1;
			state = token & 3;
		}

		if (!dist || dist > op)
			goto bad;
		LZO_NEED_OUT (len);
		for (uint i = 0; i < len; i++)
			out[op + i] = out[op - dist + i];
		op += len;
		LZO_COPY_LITERALS (state);
	}

oom:
	FREE (out);
	return ERR_OUT_OF_MEMORY;
bad:
	FREE (out);
	return ERR_INVALID_DATA;
#undef LZO_LENGTH
#undef LZO_COPY_LITERALS
#undef LZO_NEED_OUT
}

// LZO1X encoder: literal runs plus M3 matches only (distance <= 16384),
// so the instruction state machine stays trivial. Output is valid LZO1X but
// compresses a little worse than the reference encoder.
static uint lzo_hash3 (const u8 *p)
{
	return ((uint)p[0] | (uint)p[1] << 8 | (uint)p[2] << 16) * 2654435761u >> 18;
}

enumError EncodeLZO1X (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size)
		return ERR_INVALID_DATA;
	u8 *out = MALLOC ((size_t)src_size + src_size / 16 + 64);
	uint *head = MALLOC (sizeof (uint) * 16384), *prev = MALLOC (sizeof (uint) * src_size);
	if (!out || !head || !prev)
	{
		FREE (out), FREE (head), FREE (prev);
		return ERR_OUT_OF_MEMORY;
	}
	memset (head, 0xff, sizeof (uint) * 16384);
	uint dp = 0, sp = 0, lit = 0;
	bool first = true; // literal run at stream start uses the t>17 form
	// emit literals [lit, end) as a stand-alone run; `after_match` = state-0 context
	#define LZO_LITERALS(end)                                                    \
		do                                                                       \
		{                                                                        \
			const uint n = (end) - lit;                                          \
			if (n)                                                               \
			{                                                                    \
				if (first && n <= 238)                                           \
					out[dp++] = 17 + n;                                          \
				else                                                             \
				{                                                                \
					if (n - 3 <= 15 && n >= 3)                                   \
						out[dp++] = n - 3;                                       \
					else                                                         \
					{                                                            \
						uint e = n - 3 - 15;                                     \
						out[dp++] = 0;                                           \
						while (e > 255)                                          \
							out[dp++] = 0, e -= 255;                             \
						out[dp++] = e;                                           \
					}                                                            \
				}                                                                \
				memcpy (out + dp, src + lit, n);                                 \
				dp += n;                                                         \
				lit = (end);                                                     \
			}                                                                    \
			first = false;                                                       \
		} while (0)
	uint pend_match = 0, pend_dist = 0; // match waiting for its trailing-literal count
	#define LZO_EMIT_MATCH(s)                                                    \
		do                                                                       \
		{                                                                        \
			const uint d = pend_dist - 1;                                        \
			if (pend_match - 2 <= 31)                                            \
				out[dp++] = 32 | (pend_match - 2);                               \
			else                                                                 \
			{                                                                    \
				uint e = pend_match - 2 - 31;                                    \
				out[dp++] = 32;                                                  \
				while (e > 255)                                                  \
					out[dp++] = 0, e -= 255;                                     \
				out[dp++] = e;                                                   \
			}                                                                    \
			out[dp++] = (d & 63) << 2 | (s);                                     \
			out[dp++] = d >> 6;                                                  \
			pend_match = 0;                                                      \
		} while (0)
	while (sp < src_size)
	{
		uint best = 0, best_dist = 0;
		if (sp + 3 <= src_size)
		{
			const uint h = lzo_hash3 (src + sp);
			uint cand = head[h];
			for (int depth = 0; cand != 0xffffffffu && depth < 32 && sp - cand <= 16384; depth++)
			{
				uint n = 0;
				while (sp + n < src_size && src[sp + n] == src[cand + n])
					n++;
				if (n > best)
					best = n, best_dist = sp - cand;
				cand = prev[cand];
			}
			prev[sp] = head[h];
			head[h] = sp;
		}
		if (best < 3)
		{
			sp++;
			continue;
		}
		// flush the match before this one, taking up to 3 literals as its trailer
		const uint gap = sp - lit;
		if (pend_match)
		{
			if (gap <= 3)
			{
				LZO_EMIT_MATCH (gap);
				memcpy (out + dp, src + lit, gap);
				dp += gap, lit = sp;
			}
			else
			{
				LZO_EMIT_MATCH (0);
				LZO_LITERALS (sp);
			}
		}
		else
			LZO_LITERALS (sp);
		pend_match = best, pend_dist = best_dist;
		sp += best;
		lit = sp;
		first = false;
		// index skipped positions cheaply
		for (uint k = sp - best + 1; k < sp && k + 3 <= src_size; k++)
		{
			const uint h = lzo_hash3 (src + k);
			prev[k] = head[h];
			head[h] = k;
		}
	}
	if (pend_match)
	{
		const uint gap = src_size - lit;
		if (gap <= 3)
		{
			LZO_EMIT_MATCH (gap);
			memcpy (out + dp, src + lit, gap);
			dp += gap;
		}
		else
		{
			LZO_EMIT_MATCH (0);
			LZO_LITERALS (src_size);
		}
	}
	else
		LZO_LITERALS (src_size);
	out[dp++] = 0x11, out[dp++] = 0, out[dp++] = 0;
	#undef LZO_LITERALS
	#undef LZO_EMIT_MATCH
	FREE (head), FREE (prev);
	*dest = out;
	*dest_size = dp;
	return ERR_OK;
}

// iNiS "1OZL" container: magic, LE raw size, LE LZO payload size, payload.
enumError EncodeInioLZO (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	u8 *lzo = 0;
	uint lzo_size = 0;
	enumError err = EncodeLZO1X (&lzo, &lzo_size, src, src_size);
	if (err)
		return err;
	u8 *out = MALLOC ((size_t)lzo_size + 12);
	if (!out)
	{
		FREE (lzo);
		return ERR_OUT_OF_MEMORY;
	}
	memcpy (out, "1OZL", 4);
	for (uint i = 0; i < 4; i++)
		out[4 + i] = src_size >> 8 * i, out[8 + i] = lzo_size >> 8 * i;
	memcpy (out + 12, lzo, lzo_size);
	FREE (lzo);
	*dest = out;
	*dest_size = lzo_size + 12;
	return ERR_OK;
}
