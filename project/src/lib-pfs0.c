// SPDX-License-Identifier: GPL-2.0+
#include "lib-pfs0.h"
#include "lib-archive-util.h"
#include <string.h>

bool IsPFS0 (const u8 *data, uint size)
{
	if (!data || size < 16)
		return false;

	if (memcmp (data, "PFS0", 4) != 0)
		return false;

	const u32 file_count = rd_le32 (data + 4);
	const u32 string_table_size = rd_le32 (data + 8);
	const u32 reserved = rd_le32 (data + 12);

	if (reserved != 0 || file_count == 0 || file_count > 100000 || string_table_size == 0)
		return false;

	const u64 meta_size = 16 + (u64)file_count * 24 + string_table_size;
	if (size >= 16 + file_count * 24 && meta_size > (u64)size * 100) // loose sanity
		return false;

	return true;
}

bool IsHFS0 (const u8 *data, uint size)
{
	if (!data || size < 16)
		return false;

	if (memcmp (data, "HFS0", 4) != 0)
		return false;

	const u32 file_count = rd_le32 (data + 4);
	const u32 string_table_size = rd_le32 (data + 8);
	const u32 reserved = rd_le32 (data + 12);

	if (reserved != 0 || file_count == 0 || file_count > 100000 || string_table_size == 0)
		return false;

	return true;
}

enumError ExtractPFS0Archive (ccp arg, ccp basedir, uint depth)
{
	(void)depth;

	FILE *fp = fopen (arg, "rb");
	if (!fp)
		return ERR_CANT_OPEN;

	u8 header[16];
	if (fread (header, 1, 16, fp) != 16)
	{
		fclose (fp);
		return ERR_NOTHING_TO_DO;
	}

	const bool is_pfs0 = (memcmp (header, "PFS0", 4) == 0);
	const bool is_hfs0 = (memcmp (header, "HFS0", 4) == 0);

	if (!is_pfs0 && !is_hfs0)
	{
		fclose (fp);
		return ERR_NOTHING_TO_DO;
	}

	const u32 file_count = rd_le32 (header + 4);
	const u32 string_table_size = rd_le32 (header + 8);
	const u32 reserved = rd_le32 (header + 12);

	if (reserved != 0 || file_count == 0 || file_count > 100000 || string_table_size == 0 ||
		string_table_size > 0x10000000)
	{
		fclose (fp);
		return ERR_INVALID_DATA;
	}

	const uint entry_size = is_pfs0 ? 24 : 64;
	const u64 entry_table_bytes = (u64)file_count * entry_size;

	u8 *entry_table = MALLOC (entry_table_bytes);
	if (!entry_table)
	{
		fclose (fp);
		return ERR_OUT_OF_MEMORY;
	}

	if (fread (entry_table, 1, entry_table_bytes, fp) != entry_table_bytes)
	{
		FREE (entry_table);
		fclose (fp);
		return ERR_INVALID_DATA;
	}

	char *string_table = MALLOC (string_table_size + 1);
	if (!string_table)
	{
		FREE (entry_table);
		fclose (fp);
		return ERR_OUT_OF_MEMORY;
	}

	if (fread (string_table, 1, string_table_size, fp) != string_table_size)
	{
		FREE (string_table);
		FREE (entry_table);
		fclose (fp);
		return ERR_INVALID_DATA;
	}
	string_table[string_table_size] = 0;

	const u64 header_end = 16 + entry_table_bytes + string_table_size;
	const u64 data_offset = is_pfs0 ? ((header_end + 31) & ~31ULL) : ((header_end + 511) & ~511ULL);

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT %s:%s (%u files) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			is_pfs0 ? "PFS0" : "HFS0", arg, file_count, dest);

	enumError status = ERR_OK;
	const size_t chunk_size = 1024 * 1024; // 1 MB copy buffer
	u8 *chunk_buf = MALLOC (chunk_size);
	if (!chunk_buf)
	{
		FREE (string_table);
		FREE (entry_table);
		fclose (fp);
		return ERR_OUT_OF_MEMORY;
	}

	for (uint i = 0; i < file_count; i++)
	{
		const u8 *entry = entry_table + (u64)i * entry_size;
		const u64 rel_offset = rd_le64 (entry);
		const u64 file_size = rd_le64 (entry + 8);
		const u32 name_offset = rd_le32 (entry + 16);

		if (name_offset >= string_table_size)
			continue;

		const char *name = string_table + name_offset;
		char clean_name[PATH_MAX];
		if (!name[0] || !OwnedNameOk (name))
			snprintf (clean_name, sizeof (clean_name), "file_%04u.bin", i);
		else
			snprintf (clean_name, sizeof (clean_name), "%s", name);

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, clean_name);

		char *slash = strrchr (out_path, '/');
		if (slash)
		{
			*slash = 0;
			CreatePath (out_path, true);
			*slash = '/';
		}

		if (testmode)
			continue;

		FILE *out_fp = fopen (out_path, "wb");
		if (!out_fp)
		{
			status = ERR_CANT_CREATE;
			continue;
		}

		const u64 abs_offset = data_offset + rel_offset;
		if (fseeko (fp, (off_t)abs_offset, SEEK_SET) != 0)
		{
			fclose (out_fp);
			continue;
		}

		u64 remaining = file_size;
		while (remaining > 0)
		{
			const size_t to_read = (remaining > chunk_size) ? chunk_size : (size_t)remaining;
			const size_t read_bytes = fread (chunk_buf, 1, to_read, fp);
			if (read_bytes == 0)
				break;
			fwrite (chunk_buf, 1, read_bytes, out_fp);
			remaining -= read_bytes;
		}
		fclose (out_fp);
	}

	FREE (chunk_buf);
	FREE (string_table);
	FREE (entry_table);
	fclose (fp);
	return status;
}
