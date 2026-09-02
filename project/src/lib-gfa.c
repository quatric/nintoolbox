#include "lib-std.h"
#include "lib-gfa.h"
#include <string.h>
#include <errno.h>

enumError DecodeLZ10Raw (u8 *dest, uint dest_size, const u8 *src, uint src_size)
{
	if (!dest || !src)
		return EINVAL;
	uint sp = 0, dp = 0;
	while (dp < dest_size)
	{
		if (sp >= src_size)
			return EINVAL;
		u8 flags = src[sp++];
		for (uint bit = 0; bit < 8 && dp < dest_size; bit++, flags <<= 1)
		{
			if (!(flags & 0x80))
			{
				if (sp >= src_size)
					return EINVAL;
				dest[dp++] = src[sp++];
			}
			else
			{
				if (sp + 2 > src_size)
					return EINVAL;
				const u8 a = src[sp++], b = src[sp++];
				const uint len = (a >> 4) + 3, back = ((a & 15) << 8 | b) + 1;
				if (back > dp || len > dest_size - dp)
					return EINVAL;
				for (uint i = 0; i < len; i++, dp++)
					dest[dp] = dest[dp - back];
			}
		}
	}
	return ERR_OK;
}

enumError DecodeBPE (u8 *dest, uint dest_size, const u8 *src, uint src_size)
{
	if (!dest || !src)
		return EINVAL;
	uint sp = 0, dp = 0;

	while (dp < dest_size)
	{
		u8 table[256][2];
		for (uint i = 0; i < 256; i++)
		{
			table[i][0] = (u8)i;
			table[i][1] = 0;
		}
		bool paired[256];
		memset (paired, 0, sizeof (paired));

		uint c = 0;
		while (c < 256)
		{
			if (sp >= src_size)
				return EINVAL;
			uint marker = src[sp++];

			uint entries;
			if (marker > 127)
			{
				c += marker - 127;
				if (c == 256)
					break;
				entries = 1;
			}
			else
				entries = marker + 1;

			for (uint i = 0; i < entries && c < 256; i++, c++)
			{
				if (sp >= src_size)
					return EINVAL;
				const u8 lc = src[sp++];
				table[c][0] = lc;
				if (lc != (u8)c)
				{
					if (sp >= src_size)
						return EINVAL;
					table[c][1] = src[sp++];
					paired[c] = true;
				}
			}
		}

		if (sp + 2 > src_size)
			return EINVAL;
		uint block_len = (uint)src[sp] << 8 | src[sp + 1];
		sp += 2;

		u8 stack[256];
		uint sn = 0;
		while (block_len || sn)
		{
			u8 b;
			if (sn)
				b = stack[--sn];
			else
			{
				if (sp >= src_size)
					return EINVAL;
				b = src[sp++];
				block_len--;
			}

			if (paired[b])
			{
				if (sn + 2 > sizeof (stack))
					return EINVAL;
				stack[sn++] = table[b][1];
				stack[sn++] = table[b][0];
			}
			else
			{
				if (dp >= dest_size)
					return EINVAL;
				dest[dp++] = b;
			}
		}
	}
	return ERR_OK;
}

void ResetGFA (gfa_t *gfa)
{
	if (!gfa)
		return;
	FREE (gfa->blob);
	FREE (gfa->entries);
	FREE (gfa->names);
	memset (gfa, 0, sizeof (*gfa));
}

enumError ScanGFA (gfa_t *gfa, const u8 *data, uint size)
{
	if (!gfa || !data || size < 0x1c || memcmp (data, "GFAC", 4))
		return EINVAL;
	memset (gfa, 0, sizeof (*gfa));

	const u32 info_off = rd_le32 (data + 0x0c);
	const u32 data_off = rd_le32 (data + 0x14);
	const u32 data_size = rd_le32 (data + 0x18);

	if (info_off + 4 > size || data_off + 16 > size)
		return EINVAL;
	if ((u64)data_off + data_size > size)
		return EINVAL;

	const u32 n = rd_le32 (data + info_off);
	if (!n || n > 0x100000 || (u64)info_off + 4 + (u64)n * 16 > size)
		return EINVAL;

	const u8 *gfcp = data + data_off;
	if (memcmp (gfcp, "GFCP", 4))
		return EINVAL;
	const u32 zip = rd_le32 (gfcp + 8);
	const u32 out_len = rd_le32 (gfcp + 12);
	const u32 zsize = rd_le32 (gfcp + 16);
	if (!out_len || out_len > NFMT_MAX_OUTPUT)
		return EFBIG;
	if ((u64)20 + zsize > data_size)
		return EINVAL;

	u8 *blob = MALLOC (out_len);
	if (!blob)
		return ERR_CANT_CREATE;
	enumError err;
	switch (zip)
	{
		case 1:
			err = DecodeBPE (blob, out_len, gfcp + 20, zsize);
			break;
		case 2:
		case 3:
			err = DecodeLZ10Raw (blob, out_len, gfcp + 20, zsize);
			break;
		default:
			err = EINVAL;
			break;
	}
	if (err)
	{
		FREE (blob);
		return err;
	}

	gfa_entry_t *entries = CALLOC (n, sizeof (*entries));
	char *names = CALLOC (1, size);
	if (!entries || !names)
	{
		FREE (blob);
		FREE (entries);
		FREE (names);
		return ERR_CANT_CREATE;
	}
	uint name_pos = 0;

	const u8 *rec = data + info_off + 4;
	for (uint i = 0; i < n; i++, rec += 16)
	{
		u32 name_off = rd_le32 (rec + 4) & 0x00ffffff;
		const u32 fsize = rd_le32 (rec + 8);
		u32 offset = rd_le32 (rec + 12);

		if (name_off >= size)
			name_off = 0;

		entries[i].name = names + name_pos;
		if (name_off)
		{
			uint j = name_off;
			while (j < size && data[j] && name_pos + 1 < size)
				names[name_pos++] = (char)data[j++];
		}
		names[name_pos++] = 0;

		entries[i].size = fsize;
		entries[i].offset = offset >= data_off ? offset - data_off : offset;
		if (fsize && (entries[i].offset > out_len || fsize > out_len - entries[i].offset))
		{
			entries[i].size = 0;
			entries[i].offset = 0;
		}
	}

	gfa->blob = blob;
	gfa->blob_size = out_len;
	gfa->entries = entries;
	gfa->n_entries = n;
	gfa->names = names;
	gfa->compression = zip;
	return ERR_OK;
}

enumError CreateGFA (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0x100000)
		return EINVAL;

	uint payload_size = 0;
	uint names_size = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		if (!entries[i].name)
			return EINVAL;
		names_size += strlen (entries[i].name) + 1;
		payload_size += entries[i].size;
	}

	u8 *payload = CALLOC (1, payload_size ? payload_size : 1);
	if (!payload)
		return ERR_CANT_CREATE;

	uint current_offset = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		if (entries[i].size)
		{
			memcpy (payload + current_offset, entries[i].data, entries[i].size);
			current_offset += entries[i].size;
		}
	}

	u8 *zdata = 0;
	uint zsize = 0;
	enumError err = EncodeLZ10Raw (&zdata, &zsize, payload, payload_size);
	FREE (payload);
	if (err)
		return err;

	const uint info_off = 0x20;
	const uint names_off = info_off + 4 + 16 * n_entries;
	uint data_off = names_off + names_size;
	data_off = (data_off + 3) & ~3u;

	const uint data_size = 20 + zsize;
	const uint total_size = data_off + data_size;

	u8 *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (zdata);
		return ERR_CANT_CREATE;
	}

	memcpy (out, "GFAC", 4);
	wr_le32 (out + 0x0c, info_off);
	wr_le32 (out + 0x14, data_off);
	wr_le32 (out + 0x18, data_size);

	wr_le32 (out + info_off, n_entries);

	uint name_pos = 0;
	current_offset = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		u8 *rec = out + info_off + 4 + 16 * i;
		wr_le32 (rec + 4, names_off + name_pos);
		wr_le32 (rec + 8, entries[i].size);
		wr_le32 (rec + 12, data_off + current_offset);

		size_t nlen = strlen (entries[i].name) + 1;
		memcpy (out + names_off + name_pos, entries[i].name, nlen);
		name_pos += nlen;
		current_offset += entries[i].size;
	}

	u8 *gfcp = out + data_off;
	memcpy (gfcp, "GFCP", 4);
	wr_le32 (gfcp + 8, 3);
	wr_le32 (gfcp + 12, payload_size);
	wr_le32 (gfcp + 16, zsize);
	memcpy (gfcp + 20, zdata, zsize);
	FREE (zdata);

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}
