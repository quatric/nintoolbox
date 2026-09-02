#include "lib-std.h"
#include "lib-yay0.h"
#include <string.h>
#include <errno.h>
#include <limits.h>

enumError DecodeYay0 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!src || src_size < 16 || memcmp (src, "Yay0", 4))
		return EINVAL;
	const u32 out_len = rd_be32 (src + 4), link = rd_be32 (src + 8), chunk = rd_be32 (src + 12);
	enumError err = AllocOutput (dest, dest_size, out_len);
	if (err)
		return err;
	uint mask = 16, lp = link, cp = chunk, dp = 0, bits = 0;
	u32 code = 0;
	while (dp < out_len)
	{
		if (!bits)
		{
			if (mask + 4 > src_size)
				goto invalid;
			code = rd_be32 (src + mask);
			mask += 4;
			bits = 32;
		}
		if (code & 0x80000000)
		{
			if (cp >= src_size)
				goto invalid;
			(*dest)[dp++] = src[cp++];
		}
		else
		{
			if (lp + 2 > src_size)
				goto invalid;
			u16 v = (u16)src[lp] << 8 | src[lp + 1];
			lp += 2;
			uint len = v >> 12, back = (v & 0xfff) + 1;
			if (!len)
			{
				if (cp >= src_size)
					goto invalid;
				len = src[cp++] + 18;
			}
			else
				len += 2;
			if (back > dp || len > out_len - dp)
				goto invalid;
			while (len--)
				(*dest)[dp] = (*dest)[dp - back], dp++;
		}
		code <<= 1;
		bits--;
	}
	return ERR_OK;
invalid:
	FREE (*dest);
	*dest = 0;
	*dest_size = 0;
	return EINVAL;
}

enumError EncodeYay0 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > UINT_MAX / 2)
		return EINVAL;
	const uint max_masks = (src_size + 31) / 32 * 4;
	u8 *masks = CALLOC (1, max_masks);
	u8 *links = MALLOC (2 * src_size);
	u8 *chunks = MALLOC (2 * src_size);
	if (!masks || !links || !chunks)
	{
		FREE (masks);
		FREE (links);
		FREE (chunks);
		return ERR_CANT_CREATE;
	}
	uint sp = 0, mp = 0, lp = 0, cp = 0, bit = 0;
	u32 mask = 0;
	while (sp < src_size)
	{
		if (!bit)
		{
			mask = 0;
			mp += 4;
		}
		uint best_len = 0, best_back = 0;
		const uint max_back = sp < 0x1000 ? sp : 0x1000;
		const uint max_len = src_size - sp < 0x111 ? src_size - sp : 0x111;
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
			const uint disp = best_back - 1;
			if (best_len >= 18)
			{
				links[lp++] = disp >> 8;
				links[lp++] = disp;
				chunks[cp++] = best_len - 18;
			}
			else
			{
				links[lp++] = (best_len - 2) << 4 | (disp >> 8);
				links[lp++] = disp;
			}
			sp += best_len;
		}
		else
		{
			mask |= 0x80000000u >> bit;
			chunks[cp++] = src[sp++];
		}
		bit = (bit + 1) & 31;
		if (!bit)
			wr_be32 (masks + mp - 4, mask);
	}
	if (bit)
		wr_be32 (masks + mp - 4, mask);
	const uint link_off = 16 + mp;
	const uint chunk_off = link_off + lp;
	if (chunk_off > UINT_MAX - cp)
	{
		FREE (masks);
		FREE (links);
		FREE (chunks);
		return EFBIG;
	}
	const uint total = chunk_off + cp;
	u8 *out = MALLOC (total);
	if (!out)
	{
		FREE (masks);
		FREE (links);
		FREE (chunks);
		return ERR_CANT_CREATE;
	}
	memcpy (out, "Yay0", 4);
	wr_be32 (out + 4, src_size);
	wr_be32 (out + 8, link_off);
	wr_be32 (out + 12, chunk_off);
	memcpy (out + 16, masks, mp);
	memcpy (out + link_off, links, lp);
	memcpy (out + chunk_off, chunks, cp);
	FREE (masks);
	FREE (links);
	FREE (chunks);
	*dest = out;
	*dest_size = total;
	return ERR_OK;
}

//
