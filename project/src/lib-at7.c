#include "lib-std.h"
#include "lib-at7.h"
#include <string.h>
#include <errno.h>

enumError DecodeAT7 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 4)
		return EINVAL;

	*dest = 0;
	*dest_size = 0;

	if (memcmp (src, "AT7P", 4) && memcmp (src, "AT7X", 4) && memcmp (src, "AT7E", 4))
		return EINVAL;

	uint cap = src_size < 0x8000 ? 0x10000 : src_size * 2;
	if (cap < 0x10000)
		cap = 0x10000;
	u8 *out = MALLOC (cap);
	if (!out)
		return ERR_CANT_CREATE;
	uint out_pos = 0;

	uint pos = 0;
	while (pos < src_size)
	{
		if (pos + 4 > src_size)
			break;
		if (!memcmp (src + pos, "AT7E", 4))
		{
			pos += 4;
			break;
		}

		if (!memcmp (src + pos, "AT7X", 4))
		{
			if (pos + 6 > src_size)
				goto invalid_at7;
			uint block_size = rd_le16 (src + pos + 4);
			if (block_size < 6 || pos + block_size > src_size)
				goto invalid_at7;
			uint raw_len = block_size - 6;
			if (out_pos + raw_len > NFMT_MAX_OUTPUT)
				goto invalid_at7;
			if (out_pos + raw_len > cap)
			{
				cap = (out_pos + raw_len) * 2 + 0x10000;
				u8 *nout = REALLOC (out, cap);
				if (!nout)
					goto invalid_at7;
				out = nout;
			}
			memcpy (out + out_pos, src + pos + 6, raw_len);
			out_pos += raw_len;
			pos += block_size;
			continue;
		}

		if (!memcmp (src + pos, "AT7P", 4))
		{
			if (pos + 6 > src_size)
				goto invalid_at7;
			uint block_size = rd_le16 (src + pos + 4);
			if (block_size < 6 || pos + block_size > src_size)
				goto invalid_at7;
			uint block_end = pos + block_size;
			uint cur = pos + 6;

			while (cur < block_end)
			{
				u8 flag = src[cur++];
				for (int bit = 7; bit >= 0 && cur < block_end; bit--)
				{
					if (flag & (1 << bit))
					{
						if (out_pos + 1 > cap)
						{
							cap = cap * 2 + 0x10000;
							if (cap > NFMT_MAX_OUTPUT)
								goto invalid_at7;
							u8 *nout = REALLOC (out, cap);
							if (!nout)
								goto invalid_at7;
							out = nout;
						}
						out[out_pos++] = src[cur++];
					}
					else
					{
						if (cur + 1 >= block_end)
						{
							cur = block_end;
							break;
						}
						u16 token = rd_le16 (src + cur);
						cur += 2;
						if (token == 0)
							continue;

						uint match_len = (token & 0x0F) + 3;
						uint backtrack = token >> 4;
						if (backtrack == 0 || backtrack > out_pos)
							goto invalid_at7;

						if (out_pos + match_len > cap)
						{
							cap = cap * 2 + 0x10000 + match_len;
							if (cap > NFMT_MAX_OUTPUT)
								goto invalid_at7;
							u8 *nout = REALLOC (out, cap);
							if (!nout)
								goto invalid_at7;
							out = nout;
						}
						for (uint i = 0; i < match_len; i++)
						{
							out[out_pos] = out[out_pos - backtrack];
							out_pos++;
						}
					}
				}
			}
			pos = block_end;
			continue;
		}

		goto invalid_at7;
	}

	*dest = out;
	*dest_size = out_pos;
	return ERR_OK;

invalid_at7:
	FREE (out);
	return EINVAL;
}

enumError EncodeAT7 (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src)
		return EINVAL;

	*dest = 0;
	*dest_size = 0;

	if (src_size == 0)
	{
		u8 *out = MALLOC (4);
		if (!out)
			return ERR_CANT_CREATE;
		memcpy (out, "AT7E", 4);
		*dest = out;
		*dest_size = 4;
		return ERR_OK;
	}

	uint out_cap = src_size + (src_size / 8) + 0x10000;
	u8 *out = MALLOC (out_cap);
	if (!out)
		return ERR_CANT_CREATE;
	uint out_pos = 0;

	uint pos = 0;
	uint head[65536];
	uint *prev = MALLOC (0x8000 * sizeof (uint));
	if (!prev)
	{
		FREE (out);
		return ERR_CANT_CREATE;
	}

	while (pos < src_size)
	{
		uint chunk_len = src_size - pos;
		if (chunk_len > 0x7FF0)
			chunk_len = 0x7FF0;
		uint chunk_end = pos + chunk_len;

		memset (head, 0xFF, sizeof (head));
		memset (prev, 0xFF, chunk_len * sizeof (uint));

		uint block_start = out_pos;
		out_pos += 6;

		uint cpos = pos;
		while (cpos < chunk_end)
		{
			if (out_pos + 32 > out_cap)
			{
				out_cap = out_cap * 2 + 0x10000;
				u8 *nout = REALLOC (out, out_cap);
				if (!nout)
				{
					FREE (prev);
					FREE (out);
					return ERR_CANT_CREATE;
				}
				out = nout;
			}

			uint flag_pos = out_pos++;
			u8 group_flags = 0;

			for (int step = 0; step < 8 && cpos < chunk_end; step++)
			{
				uint best_len = 0;
				uint best_dist = 0;

				if (cpos + 3 <= chunk_end)
				{
					uint h = ((uint)src[cpos] << 8) ^ ((uint)src[cpos + 1] << 4)
						^ (uint)src[cpos + 2];
					h &= 0xFFFF;
					uint match_pos = head[h];

					uint chain_count = 0;
					while (match_pos != 0xFFFFFFFF && chain_count < 64)
					{
						uint dist = cpos - match_pos;
						if (dist >= 0x1000)
							break;

						uint max_m = chunk_end - cpos;
						if (max_m > 18)
							max_m = 18;

						uint m = 0;
						while (m < max_m && src[cpos + m] == src[match_pos + m])
							m++;

						if (m >= 3 && m > best_len)
						{
							best_len = m;
							best_dist = dist;
							if (m == 18)
								break;
						}

						if (match_pos < pos)
							break;
						match_pos = prev[match_pos - pos];
						chain_count++;
					}
				}

				if (best_len >= 3)
				{
					u16 token = (u16)((best_dist << 4) | ((best_len - 3) & 0x0F));
					out[out_pos++] = token & 0xFF;
					out[out_pos++] = (token >> 8) & 0xFF;

					for (uint k = 0; k < best_len; k++)
					{
						uint p_curr = cpos + k;
						if (p_curr + 2 < chunk_end)
						{
							uint h = ((uint)src[p_curr] << 8) ^ ((uint)src[p_curr + 1] << 4)
								^ (uint)src[p_curr + 2];
							h &= 0xFFFF;
							prev[p_curr - pos] = head[h];
							head[h] = p_curr;
						}
					}
					cpos += best_len;
				}
				else
				{
					group_flags |= (1 << (7 - step));
					out[out_pos++] = src[cpos];

					if (cpos + 2 < chunk_end)
					{
						uint h = ((uint)src[cpos] << 8) ^ ((uint)src[cpos + 1] << 4)
							^ (uint)src[cpos + 2];
						h &= 0xFFFF;
						prev[cpos - pos] = head[h];
						head[h] = cpos;
					}
					cpos++;
				}
			}
			out[flag_pos] = group_flags;
		}

		uint block_size = out_pos - block_start;
		if (block_size > 0xFFFF)
		{
			FREE (prev);
			FREE (out);
			return EFBIG;
		}
		memcpy (out + block_start, "AT7P", 4);
		out[block_start + 4] = block_size & 0xFF;
		out[block_start + 5] = (block_size >> 8) & 0xFF;

		pos = chunk_end;
	}

	FREE (prev);

	memcpy (out + out_pos, "AT7E", 4);
	out_pos += 4;

	*dest = out;
	*dest_size = out_pos;
	return ERR_OK;
}

enumError CreateAT7 (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries, bool compress)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xFFFF)
		return EINVAL;

	const uint toc_entry_size = 28;
	const uint n_toc_entries = n_entries + 1;
	const uint toc_size = n_toc_entries * toc_entry_size;

	u64 raw_total = toc_size;
	for (uint i = 0; i < n_entries; i++)
		raw_total += entries[i].size;

	if (raw_total > NFMT_MAX_OUTPUT)
		return EFBIG;

	u8 *raw = CALLOC (1, (size_t)raw_total);
	if (!raw)
		return ERR_CANT_CREATE;

	u32 cur_off = toc_size;
	for (uint i = 0; i < n_entries; i++)
	{
		u8 *t = raw + i * toc_entry_size;
		wr_be32 (t + 0, cur_off);
		wr_be32 (t + 4, entries[i].size);
		ccp name = entries[i].name ? entries[i].name : "";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;
		strncpy ((char *)t + 8, name, 20);

		if (entries[i].size > 0 && entries[i].data)
			memcpy (raw + cur_off, entries[i].data, entries[i].size);
		cur_off += entries[i].size;
	}
	u8 *sentinel = raw + n_entries * toc_entry_size;
	wr_be32 (sentinel + 0, 0);
	wr_be32 (sentinel + 4, 0);
	strncpy ((char *)sentinel + 8, "namesEnd", 20);

	if (compress)
	{
		u8 *comp = 0;
		uint comp_size = 0;
		enumError err = EncodeAT7 (&comp, &comp_size, raw, (uint)raw_total);
		FREE (raw);
		if (err)
			return err;
		*dest = comp;
		*dest_size = comp_size;
		return ERR_OK;
	}

	*dest = raw;
	*dest_size = (uint)raw_total;
	return ERR_OK;
}
