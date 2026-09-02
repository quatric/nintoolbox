#include "lib-std.h"
#include "lib-fsys.h"
#include <string.h>
#include <errno.h>

static bool owned_entry_add (
	nintendo_sarc_entry_t *entries, uint idx, ccp name, const u8 *data, uint size)
{
	if (!entries)
		return false;
	entries[idx].name = STRDUP (name ? name : "");
	if (!entries[idx].name)
		return false;
	entries[idx].size = size;
	if (size && data)
	{
		u8 *buf = MALLOC (size);
		if (!buf)
		{
			FREE ((char *)entries[idx].name);
			entries[idx].name = 0;
			return false;
		}
		memcpy (buf, data, size);
		entries[idx].data = buf;
	}
	else
		entries[idx].data = 0;
	return true;
}

static bool decode_fsys_lzss_lib (u8 *out, uint out_size, const u8 *in, uint in_size)
{
	if (in_size < 16 || memcmp (in, "LZSS", 4) || rd_be32 (in + 4) != out_size
		|| rd_be32 (in + 8) < 16 || rd_be32 (in + 8) > in_size)
		return false;
	const u8 *ip = in + 16, *end = in + rd_be32 (in + 8);
	u8 ring[4096];
	memset (ring, 0, sizeof (ring));
	uint op = 0, rp = 4096 - 18, flags = 0;
	while (op < out_size)
	{
		if (!(flags & 0x100))
		{
			if (ip >= end)
				return false;
			flags = 0xff00 | *ip++;
		}
		if (flags & 1)
		{
			if (ip >= end)
				return false;
			ring[rp] = out[op++] = *ip++;
			rp = (rp + 1) & 4095;
		}
		else
		{
			if (end - ip < 2)
				return false;
			uint pos = *ip++;
			const uint b = *ip++;
			pos |= (b & 0xf0) << 4;
			uint count = (b & 15) + 3;
			while (count-- && op < out_size)
			{
				ring[rp] = out[op++] = ring[pos];
				pos = (pos + 1) & 4095;
				rp = (rp + 1) & 4095;
			}
		}
		flags >>= 1;
	}
	return true;
}

static enumError encode_fsys_lzss_lib (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	uint max_out = 16 + src_size + (src_size / 8) + 256;
	u8 *out = MALLOC (max_out);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, "LZSS", 4);
	wr_be32 (out + 4, src_size);
	wr_be32 (out + 12, 0);

	u8 ring[4096];
	memset (ring, 0, sizeof (ring));
	uint rp = 4096 - 18;

	uint ip = 0;
	uint op = 16;

	u8 flag_byte = 0;
	uint flag_bit = 0;
	u8 chunk_buf[16 * 2];
	uint chunk_len = 0;

	while (ip < src_size)
	{
		uint best_len = 0;
		uint best_pos = 0;
		const uint max_match = (src_size - ip > 18) ? 18 : (src_size - ip);

		if (max_match >= 3)
		{
			for (uint i = 0; i < 4096; i++)
			{
				uint len = 0;
				while (len < max_match && ring[(i + len) & 4095] == src[ip + len])
					len++;
				if (len > best_len)
				{
					best_len = len;
					best_pos = i;
					if (best_len == max_match)
						break;
				}
			}
		}

		if (best_len >= 3)
		{
			uint p = best_pos;
			uint l = best_len - 3;
			chunk_buf[chunk_len++] = (u8)(p & 0xff);
			chunk_buf[chunk_len++] = (u8)(((p >> 4) & 0xf0) | (l & 0x0f));

			for (uint k = 0; k < best_len; k++)
			{
				ring[rp] = src[ip + k];
				rp = (rp + 1) & 4095;
			}
			ip += best_len;
		}
		else
		{
			flag_byte |= (1 << flag_bit);
			chunk_buf[chunk_len++] = src[ip];
			ring[rp] = src[ip];
			rp = (rp + 1) & 4095;
			ip++;
		}

		flag_bit++;
		if (flag_bit == 8)
		{
			out[op++] = flag_byte;
			memcpy (out + op, chunk_buf, chunk_len);
			op += chunk_len;
			flag_byte = 0;
			flag_bit = 0;
			chunk_len = 0;
		}
	}

	if (flag_bit > 0)
	{
		out[op++] = flag_byte;
		memcpy (out + op, chunk_buf, chunk_len);
		op += chunk_len;
	}

	wr_be32 (out + 8, op);
	*dest = out;
	*dest_size = op;
	return ERR_OK;
}

enumError ScanFSYS (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || size < 0x40 || memcmp (data, "FSYS", 4))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const uint n_files = rd_be32 (data + 12), table = rd_be32 (data + 24);
	if (n_files > 65536 || table > size || size - table < 12)
		return EINVAL;
	const uint file_list = rd_be32 (data + table);
	if (file_list > size || n_files > (size - file_list) / 4)
		return EINVAL;

	nintendo_sarc_entry_t *out = CALLOC (n_files, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint n = 0;
	for (uint i = 0; i < n_files; i++)
	{
		const uint ent = rd_be32 (data + file_list + i * 4);
		if (ent > size || size - ent < 40)
			continue;
		const uint offset = rd_be32 (data + ent + 4), fsize = rd_be32 (data + ent + 8);
		const uint flags = rd_be32 (data + ent + 12), csize = rd_be32 (data + ent + 20);
		const uint stored = flags & 0x80000000 ? csize : fsize;
		if (offset > size || stored > size - offset || fsize > 0x40000000)
			continue;

		char name[64];
		snprintf (name, sizeof (name), "file_%04u.bin", i);

		bool ok = false;
		if (flags & 0x80000000)
		{
			u8 *decoded = MALLOC (fsize);
			if (decoded && decode_fsys_lzss_lib (decoded, fsize, data + offset, stored))
			{
				ok = owned_entry_add (out, n, name, decoded, fsize);
			}
			FREE (decoded);
		}
		else
		{
			ok = owned_entry_add (out, n, name, data + offset, fsize);
		}

		if (!ok)
		{
			ResetOwnedEntries (out, n);
			return ERR_CANT_CREATE;
		}
		n++;
	}

	if (!n)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

enumError CreateFSYS (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries, bool compress)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 65536)
		return EINVAL;

	const uint n_files = n_entries;
	const uint table_off = 0x40;
	const uint file_list_off = table_off + 12;
	const uint desc_base = (file_list_off + n_files * 4 + 31) & ~31u;
	const uint data_start = (desc_base + n_files * 40 + 31) & ~31u;

	u8 **payloads = CALLOC (n_files, sizeof (u8 *));
	uint *payload_sizes = CALLOC (n_files, sizeof (uint));
	bool *is_compressed = CALLOC (n_files, sizeof (bool));
	if (!payloads || !payload_sizes || !is_compressed)
	{
		FREE (payloads);
		FREE (payload_sizes);
		FREE (is_compressed);
		return ERR_CANT_CREATE;
	}

	u64 total_size = data_start;
	for (uint i = 0; i < n_files; i++)
	{
		const uint uncomp_sz = entries[i].size;
		if (compress && uncomp_sz >= 16)
		{
			u8 *cdata = 0;
			uint csz = 0;
			if (encode_fsys_lzss_lib (&cdata, &csz, entries[i].data, uncomp_sz) == ERR_OK && cdata && csz < uncomp_sz)
			{
				payloads[i] = cdata;
				payload_sizes[i] = csz;
				is_compressed[i] = true;
			}
			else
			{
				FREE (cdata);
				payloads[i] = (u8 *)entries[i].data;
				payload_sizes[i] = uncomp_sz;
				is_compressed[i] = false;
			}
		}
		else
		{
			payloads[i] = (u8 *)entries[i].data;
			payload_sizes[i] = uncomp_sz;
			is_compressed[i] = false;
		}

		total_size += payload_sizes[i];
		total_size = (total_size + 31) & ~31u;
	}

	if (total_size > 0x7fffffff)
	{
		for (uint i = 0; i < n_files; i++)
			if (is_compressed[i]) FREE (payloads[i]);
		FREE (payloads);
		FREE (payload_sizes);
		FREE (is_compressed);
		return EFBIG;
	}

	u8 *out = CALLOC (1, (size_t)total_size);
	if (!out)
	{
		for (uint i = 0; i < n_files; i++)
			if (is_compressed[i]) FREE (payloads[i]);
		FREE (payloads);
		FREE (payload_sizes);
		FREE (is_compressed);
		return ERR_CANT_CREATE;
	}

	memcpy (out, "FSYS", 4);
	wr_be32 (out + 4, (u32)total_size);
	wr_be32 (out + 8, 0);
	wr_be32 (out + 12, n_files);
	wr_be32 (out + 16, 0);
	wr_be32 (out + 20, 0);
	wr_be32 (out + 24, table_off);

	wr_be32 (out + table_off, file_list_off);

	u32 cur_data_off = data_start;
	for (uint i = 0; i < n_files; i++)
	{
		const u32 desc_off = desc_base + i * 40;
		wr_be32 (out + file_list_off + i * 4, desc_off);

		u8 *desc = out + desc_off;
		wr_be32 (desc + 0, i);
		wr_be32 (desc + 4, cur_data_off);
		wr_be32 (desc + 8, entries[i].size);
		wr_be32 (desc + 12, is_compressed[i] ? 0x80000000u : 0);
		wr_be32 (desc + 16, 0);
		wr_be32 (desc + 20, payload_sizes[i]);

		if (payloads[i] && payload_sizes[i])
			memcpy (out + cur_data_off, payloads[i], payload_sizes[i]);

		cur_data_off += payload_sizes[i];
		cur_data_off = (cur_data_off + 31) & ~31u;

		if (is_compressed[i])
			FREE (payloads[i]);
	}

	FREE (payloads);
	FREE (payload_sizes);
	FREE (is_compressed);

	*dest = out;
	*dest_size = (uint)total_size;
	return ERR_OK;
}
