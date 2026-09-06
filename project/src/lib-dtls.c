#include "lib-std.h"
#include "lib-dtls.h"
#include <string.h>
#include <zlib.h>
#include <errno.h>

enumError ScanDTLS (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *ls_data, uint ls_size,
	const u8 *dt_data, uint dt_size)
{
	if (!entries || !n_entries || !ls_data || ls_size < 8)
		return EINVAL;

	bool is_be = false;
	uint count = 0;
	uint entry_sz = 24;
	size_t table_off = 8;

	if (!memcmp (ls_data, "LS\0\0", 4))
	{
		is_be = true;
		count = rd_be32 (ls_data + 4);
	}
	else if (!memcmp (ls_data, "\0\0SL", 4) || (!memcmp (ls_data, "SL\0\0", 4)))
	{
		is_be = false;
		count = rd_le32 (ls_data + 4);
	}
	else
	{
		count = rd_be32 (ls_data);
		if (count && count < 0x100000 && (size_t)count * 24 + 4 <= ls_size)
		{
			is_be = true;
			table_off = 4;
		}
		else
		{
			count = rd_le32 (ls_data);
			if (count && count < 0x100000 && (size_t)count * 24 + 4 <= ls_size)
			{
				is_be = false;
				table_off = 4;
			}
			else
				return EINVAL;
		}
	}

	if (!count || count > 0x100000 || table_off + (size_t)count * entry_sz > ls_size)
		return EINVAL;

	nintendo_sarc_entry_t *res = CALLOC (count, sizeof (*res));
	if (!res)
		return ERR_CANT_CREATE;

	for (uint i = 0; i < count; i++)
	{
		const u8 *e = ls_data + table_off + i * entry_sz;
		const u32 hash = is_be ? rd_be32 (e) : rd_le32 (e);
		const u16 flags = is_be ? rd_be16 (e + 6) : rd_le16 (e + 6);
		const u32 off = is_be ? rd_be32 (e + 8) : rd_le32 (e + 8);
		const u32 comp_sz = is_be ? rd_be32 (e + 12) : rd_le32 (e + 12);
		const u32 decomp_sz = is_be ? rd_be32 (e + 16) : rd_le32 (e + 16);

		char name[64];
		snprintf (name, sizeof (name), "%08X.bin", hash ? hash : i);
		res[i].name = STRDUP (name);

		if (dt_data && off + comp_sz <= dt_size)
		{
			if ((flags & 1) && decomp_sz > comp_sz)
			{
				u8 *dec = MALLOC (decomp_sz);
				uLongf dsz = decomp_sz;
				if (dec && uncompress (dec, &dsz, dt_data + off, comp_sz) == Z_OK)
				{
					res[i].data = dec;
					res[i].size = (uint)dsz;
				}
				else
				{
					FREE (dec);
					res[i].data = dt_data + off;
					res[i].size = comp_sz;
				}
			}
			else
			{
				res[i].data = dt_data + off;
				res[i].size = comp_sz;
			}
		}
	}

	*entries = res;
	*n_entries = count;
	return ERR_OK;
}

enumError CreateDTLS (
	u8 **out_ls, uint *out_ls_size, u8 **out_dt, uint *out_dt_size,
	const nintendo_sarc_entry_t *entries, uint n_entries, bool compress, bool big_endian)
{
	if (!out_ls || !out_ls_size || !out_dt || !out_dt_size || !entries || !n_entries)
		return EINVAL;

	const size_t ls_total = 8 + (size_t)n_entries * 24;
	u8 *ls = CALLOC (1, ls_total);
	if (!ls)
		return ERR_CANT_CREATE;

	memcpy (ls, big_endian ? "LS\0\0" : "\0\0SL", 4);
	if (big_endian)
		wr_be32 (ls + 4, n_entries);
	else
		wr_le32 (ls + 4, n_entries);

	size_t dt_capacity = 0;
	for (uint i = 0; i < n_entries; i++)
		dt_capacity += (entries[i].size + 31) & ~31u;
	if (dt_capacity == 0)
		dt_capacity = 32;

	u8 *dt = CALLOC (1, dt_capacity);
	if (!dt)
	{
		FREE (ls);
		return ERR_CANT_CREATE;
	}

	uint cur_dt_off = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		u8 *e = ls + 8 + i * 24;
		u32 hash = 0;
		if (entries[i].name)
		{
			ccp fname = entries[i].name;
			ccp slash = strrchr (fname, '/');
			if (slash)
				fname = slash + 1;
			char *endptr = 0;
			u32 val = (u32)strtoul (fname, &endptr, 16);
			if (endptr && (*endptr == '.' || *endptr == '\0') && (endptr - fname) == 8)
				hash = val;
			else
			{
				for (const char *p = entries[i].name; *p; p++)
					hash = hash * 31 + (u8)*p;
			}
		}

		u8 *payload = (u8 *)entries[i].data;
		uint comp_sz = entries[i].size;
		uint decomp_sz = entries[i].size;
		uint flags = 0;
		bool is_alloced = false;

		if (compress && entries[i].size > 64)
		{
			uLongf bound = compressBound (entries[i].size);
			u8 *comp = MALLOC (bound);
			if (comp && compress2 (comp, &bound, entries[i].data, entries[i].size, 6) == Z_OK && bound < entries[i].size)
			{
				payload = comp;
				comp_sz = (uint)bound;
				flags |= 1;
				is_alloced = true;
			}
			else
				FREE (comp);
		}

		if (cur_dt_off + comp_sz > dt_capacity)
		{
			dt_capacity = (cur_dt_off + comp_sz + 4095) & ~4095u;
			u8 *new_dt = REALLOC (dt, dt_capacity);
			if (!new_dt)
			{
				if (is_alloced) FREE (payload);
				FREE (ls); FREE (dt);
				return ERR_CANT_CREATE;
			}
			dt = new_dt;
		}

		if (payload && comp_sz)
			memcpy (dt + cur_dt_off, payload, comp_sz);

		if (big_endian)
		{
			wr_be32 (e + 0, hash);
			wr_be16 (e + 4, 0);
			wr_be16 (e + 6, flags);
			wr_be32 (e + 8, cur_dt_off);
			wr_be32 (e + 12, comp_sz);
			wr_be32 (e + 16, decomp_sz);
			wr_be32 (e + 20, 0);
		}
		else
		{
			wr_le32 (e + 0, hash);
			wr_le16 (e + 4, 0);
			wr_le16 (e + 6, flags);
			wr_le32 (e + 8, cur_dt_off);
			wr_le32 (e + 12, comp_sz);
			wr_le32 (e + 16, decomp_sz);
			wr_le32 (e + 20, 0);
		}

		cur_dt_off += (comp_sz + 31) & ~31u;
		if (is_alloced)
			FREE (payload);
	}

	*out_ls = ls;
	*out_ls_size = (uint)ls_total;
	*out_dt = dt;
	*out_dt_size = cur_dt_off;
	return ERR_OK;
}
