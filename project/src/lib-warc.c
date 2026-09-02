#include "lib-std.h"
#include "lib-warc.h"
#include <string.h>
#include <errno.h>

void ResetWARC (warc_t *warc)
{
	if (!warc)
		return;
	FREE (warc->entries);
	FREE (warc->names);
	memset (warc, 0, sizeof (*warc));
}

enumError ScanWARC (warc_t *warc, const u8 *data, uint size)
{
	if (!warc || !data || size < 64 || memcmp (data, "WARC", 4))
		return EINVAL;
	memset (warc, 0, sizeof (*warc));

	uint off = 4;
	off += 16;
	const uint folders = rd_be16 (data + off);
	off += 2;
	const uint files = rd_be16 (data + off);
	off += 2;
	off += 8 * 4;
	const u32 entries = rd_be32 (data + off);
	off += 4;
	off += 4;

	if (files > 0x100000 || folders > 0x10000)
		return EINVAL;
	if ((u64)off + (u64)files * 32 > size)
		return EINVAL;

	u32 *file_off = MALLOC (files * sizeof (u32));
	u32 *file_size = MALLOC (files * sizeof (u32));
	if (!file_off || !file_size)
	{
		FREE (file_off);
		FREE (file_size);
		return ERR_CANT_CREATE;
	}

	for (uint i = 0; i < files; i++, off += 32)
	{
		const u8 *h = data + off;
		file_size[i] = rd_be32 (h + 20);
		file_off[i] = rd_be32 (h + 28);
	}

	if ((u64)off + ((u64)entries + 1) * 16 > size)
	{
		FREE (file_off);
		FREE (file_size);
		return EINVAL;
	}
	off += ((uint)entries + 1) * 16;

	char path[256] = "";
	for (uint i = 0; i < folders; i++)
	{
		uint start = off;
		while (off < size && data[off])
			off++;
		if (off >= size)
		{
			FREE (file_off);
			FREE (file_size);
			return EINVAL;
		}
		if (!i)
		{
			uint len = off - start;
			if (len >= sizeof (path))
				len = sizeof (path) - 1;
			memcpy (path, data + start, len);
			path[len] = 0;
		}
		off = off + 1;
		off = (off + 3) & ~3u;
	}

	warc_entry_t *out_entries = CALLOC (files, sizeof (*out_entries));
	char *names = CALLOC (1, size);
	if (!out_entries || !names)
	{
		FREE (file_off);
		FREE (file_size);
		FREE (out_entries);
		FREE (names);
		return ERR_CANT_CREATE;
	}
	uint name_pos = 0;
	uint pathlen = (uint)strlen (path);

	uint i;
	for (i = 0; i < files; i++)
	{
		uint start = off;
		while (off < size && data[off])
			off++;
		if (off >= size)
			break;
		uint namelen = off - start;
		off = off + 1;
		off = (off + 3) & ~3u;

		if ((u64)file_off[i] + file_size[i] > size)
			continue;

		out_entries[i].name = names + name_pos;
		if (pathlen)
		{
			memcpy (names + name_pos, path, pathlen);
			name_pos += pathlen;
			names[name_pos++] = '/';
		}
		memcpy (names + name_pos, data + start, namelen);
		name_pos += namelen;
		names[name_pos++] = 0;

		out_entries[i].data = data + file_off[i];
		out_entries[i].size = file_size[i];
	}

	FREE (file_off);
	FREE (file_size);

	if (!i)
	{
		FREE (out_entries);
		FREE (names);
		return EINVAL;
	}
	warc->data = data;
	warc->size = size;
	warc->entries = out_entries;
	warc->n_entries = i;
	warc->names = names;
	return ERR_OK;
}

enumError CreateWARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0x100000)
		return EINVAL;

	ccp folder = "";
	uint folder_len = 0;
	{
		ccp n0 = entries[0].name ? entries[0].name : "";
		ccp slash = strrchr (n0, '/');
		if (slash)
		{
			folder = n0;
			folder_len = (uint)(slash - n0);
		}
	}
	for (uint i = 1; folder_len && i < n_entries; i++)
	{
		ccp name = entries[i].name ? entries[i].name : "";
		if (strncmp (name, folder, folder_len) || name[folder_len] != '/')
			folder_len = 0;
	}
	const uint folders = folder_len ? 1 : 0;

	ccp *fname = MALLOC (n_entries * sizeof (ccp));
	if (!fname)
		return ERR_CANT_CREATE;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = entries[i].name ? entries[i].name : "";
		if (folder_len && !strncmp (name, folder, folder_len) && name[folder_len] == '/')
			fname[i] = name + folder_len + 1;
		else
			fname[i] = name;
	}

	u64 total = 64 + (u64)n_entries * 32 + ((u64)n_entries + 1) * 16;
	if (folders)
	{
		total += folder_len + 1;
		total = (total + 3) & ~(u64)3;
	}
	for (uint i = 0; i < n_entries; i++)
	{
		total += strlen (fname[i]) + 1;
		total = (total + 3) & ~(u64)3;
	}
	const u64 names_end = total;
	for (uint i = 0; i < n_entries; i++)
		total += entries[i].size;

	if (total > NFMT_MAX_OUTPUT)
	{
		FREE (fname);
		return EFBIG;
	}

	u8 *out = CALLOC (1, (size_t)total);
	if (!out)
	{
		FREE (fname);
		return ERR_CANT_CREATE;
	}

	memcpy (out, "WARC", 4);
	wr_be32 (out + 4, 0);
	wr_be32 (out + 8, 0);
	wr_be32 (out + 12, (u32)total);
	wr_be32 (out + 16, (u32)names_end);
	wr_be16 (out + 20, (u16)folders);
	wr_be16 (out + 22, (u16)n_entries);
	for (uint i = 0; i < 8; i++)
		wr_be32 (out + 24 + i * 4, 0);
	wr_be32 (out + 56, n_entries);
	wr_be32 (out + 60, 0);

	u32 *data_off = MALLOC (n_entries * sizeof (u32));
	if (!data_off)
	{
		FREE (fname);
		FREE (out);
		return ERR_CANT_CREATE;
	}

	uint off = 64;
	u32 cur_off = (u32)names_end;
	for (uint i = 0; i < n_entries; i++, off += 32)
	{
		u8 *h = out + off;
		wr_be32 (h + 20, entries[i].size);
		wr_be32 (h + 28, cur_off);
		data_off[i] = cur_off;
		cur_off += entries[i].size;
	}

	off += ((uint)n_entries + 1) * 16;

	if (folders)
	{
		memcpy (out + off, folder, folder_len);
		off += folder_len + 1;
		off = (off + 3) & ~3u;
	}

	for (uint i = 0; i < n_entries; i++)
	{
		size_t len = strlen (fname[i]);
		memcpy (out + off, fname[i], len);
		off += (uint)len + 1;
		off = (off + 3) & ~3u;
	}

	for (uint i = 0; i < n_entries; i++)
	{
		if (entries[i].size && entries[i].data)
			memcpy (out + data_off[i], entries[i].data, entries[i].size);
	}

	FREE (fname);
	FREE (data_off);
	*dest = out;
	*dest_size = (uint)total;
	return ERR_OK;
}
