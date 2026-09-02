#include "lib-std.h"
#include "lib-blz.h"
#include <string.h>
#include <errno.h>
#include <limits.h>

// ndstool-staged arm9.bin/arm7.bin/overlay), not from generic dispatch.
enumError DecodeBLZ (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!src || src_size < 4)
		return EINVAL;

	const u32 inc_len = rd_le32 (src + src_size - 4);
	if (!inc_len)
	{
		// "not coded" marker: BLZ_Encode() writes this when compression
		// would have made the file bigger. Confirmed against the real
		// reference decoder rather than assumed: despite what the encoder
		// side suggests, "decoding" this case reproduces the *entire*
		// input verbatim, trailing 4-byte zero marker included, not the
		// marker-stripped plain content -- checked with `blz -d` on a
		// deliberately incompressible sample and diffed byte-for-byte.
		enumError err = AllocOutput (dest, dest_size, src_size);
		if (err)
			return err;
		memcpy (*dest, src, src_size);
		return ERR_OK;
	}

	if (src_size < 8)
		return EINVAL;
	const uint hdr_len = src[src_size - 5];
	if (hdr_len < 8 || hdr_len > 11 || src_size <= hdr_len)
		return EINVAL;

	const u32 enc_len = rd_le32 (src + src_size - 8) & 0x00FFFFFF;
	if (enc_len > src_size || enc_len < hdr_len)
		return EINVAL;
	const u32 dec_len = (u32)src_size - enc_len; // leading plain span
	const u32 pak_len = enc_len - hdr_len; // compressed span
	if (dec_len + pak_len > src_size)
		return EINVAL;

	const u64 raw_len64 = (u64)dec_len + enc_len + inc_len;
	if (raw_len64 > 64 * 1024 * 1024)
		return EINVAL; // sanity cap
	const u32 raw_len = (u32)raw_len64;

	enumError err = AllocOutput (dest, dest_size, raw_len);
	if (err)
		return err;
	u8 *raw = *dest;

	// Leading dec_len bytes are stored verbatim (not part of the
	// compressed span at all).
	memcpy (raw, src, dec_len);

	// Reverse a private copy of the compressed span so ordinary
	// forward-reading LZSS logic reproduces BLZ_Encode()'s backward walk.
	u8 *rev = MALLOC (pak_len ? pak_len : 1);
	for (u32 i = 0; i < pak_len; i++)
		rev[i] = src[dec_len + pak_len - 1 - i];

	u32 rp = 0, dp = dec_len;
	u8 flags = 0, mask = 0;
	bool bad = false;
	while (dp < raw_len)
	{
		if (!(mask >>= 1))
		{
			if (rp >= pak_len)
				break;
			flags = rev[rp++];
			mask = 0x80;
		}
		if (!(flags & mask))
		{
			if (rp >= pak_len)
			{
				bad = true;
				break;
			}
			raw[dp++] = rev[rp++];
		}
		else
		{
			if (rp + 1 >= pak_len)
			{
				bad = true;
				break;
			}
			uint pos = (uint)rev[rp] << 8 | rev[rp + 1];
			rp += 2;
			uint len = (pos >> 12) + 3;
			uint back = (pos & 0xFFF) + 3;
			if (back > dp - dec_len || dp + len > raw_len)
			{
				bad = true;
				break;
			}
			while (len--)
			{
				raw[dp] = raw[dp - back];
				dp++;
			}
		}
	}
	FREE (rev);

	if (bad || dp != raw_len)
	{
		FREE (*dest);
		*dest = 0;
		*dest_size = 0;
		return EINVAL;
	}

	// Un-reverse the newly-decoded tail back to normal forward order (the
	// leading dec_len verbatim span was never reversed and stays as-is).
	for (u32 i = dec_len, j = raw_len - 1; i < j; i++, j--)
	{
		u8 t = raw[i];
		raw[i] = raw[j];
		raw[j] = t;
	}

	return ERR_OK;
}

enumError EncodeBLZ (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > 0x00FFFFFF)
		return EINVAL;

	u8 *rev_src = MALLOC (src_size);
	if (!rev_src)
		return ERR_CANT_CREATE;
	for (uint i = 0; i < src_size; i++)
		rev_src[i] = src[src_size - 1 - i];

	const uint max_pak = src_size + (src_size + 7) / 8 + 32;
	u8 *rev_pak = MALLOC (max_pak);
	if (!rev_pak)
	{
		FREE (rev_src);
		return ERR_CANT_CREATE;
	}

	uint sp = 0, dp = 0;
	while (sp < src_size)
	{
		const uint flags_pos = dp++;
		u8 flags = 0;
		for (uint bit = 0; bit < 8 && sp < src_size; bit++)
		{
			uint best_len = 0, best_back = 0;
			const uint max_back = sp < 4098 ? sp : 4098;
			const uint max_len = src_size - sp < 18 ? src_size - sp : 18;
			for (uint back = 3; back <= max_back; back++)
			{
				uint len = 0;
				while (len < max_len && rev_src[sp + len] == rev_src[sp - back + len])
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
				flags |= (0x80 >> bit);
				const uint pos = ((best_len - 3) << 12) | ((best_back - 3) & 0xFFF);
				rev_pak[dp++] = (pos >> 8) & 0xFF;
				rev_pak[dp++] = pos & 0xFF;
				sp += best_len;
			}
			else
			{
				rev_pak[dp++] = rev_src[sp++];
			}
		}
		rev_pak[flags_pos] = flags;
	}
	FREE (rev_src);

	const uint pak_len = dp;
	if (pak_len + 8 >= src_size)
	{
		FREE (rev_pak);
		u8 *out = CALLOC (1, src_size + 4);
		if (!out)
			return ERR_CANT_CREATE;
		memcpy (out, src, src_size);
		*dest = out;
		*dest_size = src_size + 4;
		return ERR_OK;
	}

	const uint enc_len = pak_len + 8;
	const uint inc_len = src_size - enc_len;
	u8 *out = MALLOC (enc_len);
	if (!out)
	{
		FREE (rev_pak);
		return ERR_CANT_CREATE;
	}

	for (uint i = 0; i < pak_len; i++)
		out[i] = rev_pak[pak_len - 1 - i];
	FREE (rev_pak);

	out[pak_len + 0] = enc_len & 0xFF;
	out[pak_len + 1] = (enc_len >> 8) & 0xFF;
	out[pak_len + 2] = (enc_len >> 16) & 0xFF;
	out[pak_len + 3] = 8;
	out[pak_len + 4] = inc_len & 0xFF;
	out[pak_len + 5] = (inc_len >> 8) & 0xFF;
	out[pak_len + 6] = (inc_len >> 16) & 0xFF;
	out[pak_len + 7] = (inc_len >> 24) & 0xFF;

	*dest = out;
	*dest_size = enc_len;
	return ERR_OK;
}

