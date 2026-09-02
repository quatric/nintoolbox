#include "lib-std.h"
#include "lib-camelot.h"
#include <string.h>
#include <errno.h>
#include <limits.h>

enumError DecodeCamelot (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!src || src_size < 5 || (src[0] != 1 && src[0] != 2))
		return EINVAL;
	const u32 out_len = ((u32)src[1] << 16) | ((u32)src[2] << 8) | src[3];
	enumError err = AllocOutput (dest, dest_size, out_len);
	if (err)
		return err;
	uint sp = 4, dp = 0;
	while (sp < src_size && dp < out_len)
	{
		const u8 flags = src[sp++];
		for (uint bit = 0; bit < 8 && dp < out_len; bit++)
		{
			if (flags & (0x80 >> bit))
			{
				if (sp + 2 > src_size)
					goto invalid;
				const u8 a = src[sp++], b = src[sp++];
				const uint back = ((uint)(a >> 4) << 8) | b;
				uint len = a & 15;
				if (!len)
				{
					if (sp >= src_size)
						goto invalid;
					len = src[sp++] + 17;
				}
				else
					len++;
				if (!back || len > out_len - dp)
					goto invalid;

				// Camelot's window is zero-filled before the first output byte.
				// Early references in real STPL headers deliberately use that area.
				while (len--)
				{
					(*dest)[dp] = back <= dp ? (*dest)[dp - back] : 0;
					dp++;
				}
			}
			else
			{
				if (sp >= src_size)
					goto invalid;
				(*dest)[dp++] = src[sp++];
			}
		}
	}
	if (dp == out_len)
		return ERR_OK;
invalid:
	FREE (*dest);
	*dest = 0;
	*dest_size = 0;
	return EINVAL;
}

enumError EncodeCamelot (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !src)
		return EINVAL;
	if (src_size > 0x00FFFFFF)
		return EFBIG;

	const uint max_out = 4 + src_size + (src_size + 7) / 8 + 32;
	u8 *out = MALLOC (max_out);
	if (!out)
		return ERR_CANT_CREATE;

	out[0] = 1;
	out[1] = (u8)(src_size >> 16);
	out[2] = (u8)(src_size >> 8);
	out[3] = (u8)(src_size);

	uint out_pos = 4;
	uint p = 0;

	int head[65536];
	memset (head, -1, sizeof (head));
	int *prev = src_size ? MALLOC (src_size * sizeof (int)) : 0;
	if (src_size && !prev)
	{
		FREE (out);
		return ERR_CANT_CREATE;
	}

	while (p < src_size)
	{
		uint flags_pos = out_pos++;
		u8 flags = 0;
		for (uint bit = 0; bit < 8 && p < src_size; bit++)
		{
			uint best_len = 0, best_dist = 0;
			const uint max_len = (src_size - p > 272) ? 272 : (src_size - p);
			if (max_len >= 2)
			{
				u16 h = ((u16)src[p] << 8) | src[p + 1];
				int cand = head[h];
				uint chain_len = 64;
				while (cand >= 0 && chain_len-- > 0)
				{
					uint dist = p - cand;
					if (dist > 4095)
						break;
					uint l = 0;
					while (l < max_len && src[cand + l] == src[p + l])
						l++;
					if (l > best_len)
					{
						best_len = l;
						best_dist = dist;
						if (best_len == 272)
							break;
					}
					cand = prev[cand];
				}
			}

			if (best_len >= 2 && best_dist > 0)
			{
				flags |= (0x80 >> bit);
				if (best_len <= 16)
				{
					out[out_pos++] = (u8)(((best_dist >> 8) << 4) | (best_len - 1));
					out[out_pos++] = (u8)(best_dist & 0xFF);
				}
				else
				{
					out[out_pos++] = (u8)(((best_dist >> 8) << 4) | 0);
					out[out_pos++] = (u8)(best_dist & 0xFF);
					out[out_pos++] = (u8)(best_len - 17);
				}
				for (uint i = 0; i < best_len; i++)
				{
					if (p + i + 1 < src_size)
					{
						u16 h = ((u16)src[p + i] << 8) | src[p + i + 1];
						prev[p + i] = head[h];
						head[h] = p + i;
					}
				}
				p += best_len;
			}
			else
			{
				out[out_pos++] = src[p];
				if (p + 1 < src_size)
				{
					u16 h = ((u16)src[p] << 8) | src[p + 1];
					prev[p] = head[h];
					head[h] = p;
				}
				p++;
			}
		}
		out[flags_pos] = flags;
	}

	if (prev)
		FREE (prev);
	*dest = out;
	if (dest_size)
		*dest_size = out_pos;
	return ERR_OK;
}

