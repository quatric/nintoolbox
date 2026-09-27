// SPDX-License-Identifier: GPL-2.0+
#include "lib-pfs0.h"
#include "lib-archive-util.h"
#include "lib-sha256.h"
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

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

static inline void wr_le64 (u8 *p, u64 v)
{
	wr_le32 (p, (u32)v);
	wr_le32 (p + 4, (u32)(v >> 32));
}

typedef struct pfs0_file_entry_t
{
	char name[PATH_MAX];
	char path[PATH_MAX];
	u64 size;
	u64 rel_offset;
	u32 name_offset;
	u8 hash[32];
} pfs0_file_entry_t;

static int compare_pfs0_entries (const void *a, const void *b)
{
	const pfs0_file_entry_t *ea = (const pfs0_file_entry_t *)a;
	const pfs0_file_entry_t *eb = (const pfs0_file_entry_t *)b;
	return strcmp (ea->name, eb->name);
}

enumError CreatePFS0Archive (ccp source_dir, ccp dest_file, bool is_hfs0)
{
	DIR *dir = opendir (source_dir);
	if (!dir)
		return ERR_CANT_OPEN;

	pfs0_file_entry_t *files = NULL;
	uint count = 0, capacity = 0;
	u32 string_table_size = 0;

	struct dirent *de;
	while ((de = readdir (dir)) != NULL)
	{
		if (de->d_name[0] == '.')
			continue;
		if (!strcmp (de->d_name, ".DS_Store"))
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s", source_dir, de->d_name);
		struct stat st;
		if (stat (path, &st) != 0 || !S_ISREG (st.st_mode))
			continue;

		if (count >= capacity)
		{
			capacity = capacity ? capacity * 2 : 16;
			pfs0_file_entry_t *new_files = REALLOC (files, capacity * sizeof (pfs0_file_entry_t));
			if (!new_files)
			{
				FREE (files);
				closedir (dir);
				return ERR_OUT_OF_MEMORY;
			}
			files = new_files;
		}

		snprintf (files[count].name, sizeof (files[count].name), "%s", de->d_name);
		snprintf (files[count].path, sizeof (files[count].path), "%s", path);
		files[count].size = (u64)st.st_size;
		memset (files[count].hash, 0, sizeof (files[count].hash));
		count++;
	}
	closedir (dir);

	if (count == 0)
	{
		FREE (files);
		return ERR_NOTHING_TO_DO;
	}

	qsort (files, count, sizeof (pfs0_file_entry_t), compare_pfs0_entries);

	// Compute string table offsets and total string table size
	for (uint i = 0; i < count; i++)
	{
		files[i].name_offset = string_table_size;
		string_table_size += (u32)strlen (files[i].name) + 1;
	}

	// Compute file data offsets
	u64 cur_offset = 0;
	for (uint i = 0; i < count; i++)
	{
		files[i].rel_offset = cur_offset;
		cur_offset += files[i].size;
		if (is_hfs0)
			cur_offset = (cur_offset + 511ULL) & ~511ULL;
	}

	const uint entry_size = is_hfs0 ? 64 : 24;
	const u64 entry_table_bytes = (u64)count * entry_size;
	const u64 meta_size = 16 + entry_table_bytes + string_table_size;
	const u64 align_size = is_hfs0 ? 512 : 32;
	const u64 aligned_meta_size = (meta_size + (align_size - 1)) & ~(align_size - 1);

	u8 *meta_buf = CALLOC ((size_t)aligned_meta_size, 1);
	if (!meta_buf)
	{
		FREE (files);
		return ERR_OUT_OF_MEMORY;
	}

	// Write header
	memcpy (meta_buf, is_hfs0 ? "HFS0" : "PFS0", 4);
	wr_le32 (meta_buf + 4, count);
	wr_le32 (meta_buf + 8, string_table_size);
	wr_le32 (meta_buf + 12, 0);

	// If HFS0, compute SHA256 of each file
	if (is_hfs0)
	{
		const size_t chk_sz = 1024 * 1024;
		u8 *chk_buf = MALLOC (chk_sz);
		if (chk_buf)
		{
			for (uint i = 0; i < count; i++)
			{
				FILE *f = fopen (files[i].path, "rb");
				if (!f)
					continue;
				sha256_ctx_t ctx;
				sha256_init (&ctx);
				u64 rem = files[i].size;
				while (rem > 0)
				{
					size_t r = fread (chk_buf, 1, rem > chk_sz ? chk_sz : (size_t)rem, f);
					if (r == 0)
						break;
					sha256_update (&ctx, chk_buf, r);
					rem -= r;
				}
				fclose (f);
				sha256_final (&ctx, files[i].hash);
			}
			FREE (chk_buf);
		}
	}

	// Fill entries
	for (uint i = 0; i < count; i++)
	{
		u8 *entry = meta_buf + 16 + (u64)i * entry_size;
		wr_le64 (entry + 0, files[i].rel_offset);
		wr_le64 (entry + 8, files[i].size);
		wr_le32 (entry + 16, files[i].name_offset);
		if (is_hfs0)
		{
			wr_le32 (entry + 20, (u32)(files[i].size > 512 ? 512 : files[i].size));
			wr_le64 (entry + 24, 0);
			memcpy (entry + 32, files[i].hash, 32);
		}
		else
		{
			wr_le32 (entry + 20, 0);
		}
	}

	// Fill string table
	char *str_table = (char *)(meta_buf + 16 + entry_table_bytes);
	for (uint i = 0; i < count; i++)
		strcpy (str_table + files[i].name_offset, files[i].name);

	// Ensure destination directory exists
	char *slash = strrchr ((char *)dest_file, '/');
	if (slash)
	{
		char dirpath[PATH_MAX];
		size_t dlen = (size_t)(slash - dest_file);
		if (dlen < sizeof (dirpath))
		{
			memcpy (dirpath, dest_file, dlen);
			dirpath[dlen] = 0;
			CreatePath (dirpath, true);
		}
	}

	FILE *out_fp = fopen (dest_file, "wb");
	if (!out_fp)
	{
		FREE (meta_buf);
		FREE (files);
		return ERR_CANT_CREATE;
	}

	if (fwrite (meta_buf, 1, (size_t)aligned_meta_size, out_fp) != aligned_meta_size)
	{
		fclose (out_fp);
		FREE (meta_buf);
		FREE (files);
		return ERR_CANT_CREATE;
	}
	FREE (meta_buf);

	const size_t copy_chunk = 1024 * 1024;
	u8 *copy_buf = MALLOC (copy_chunk);
	if (!copy_buf)
	{
		fclose (out_fp);
		FREE (files);
		return ERR_OUT_OF_MEMORY;
	}

	enumError status = ERR_OK;
	for (uint i = 0; i < count; i++)
	{
		FILE *in_fp = fopen (files[i].path, "rb");
		if (!in_fp)
		{
			status = ERR_CANT_OPEN;
			break;
		}

		u64 rem = files[i].size;
		while (rem > 0)
		{
			size_t to_read = rem > copy_chunk ? copy_chunk : (size_t)rem;
			size_t read_bytes = fread (copy_buf, 1, to_read, in_fp);
			if (read_bytes == 0)
				break;
			if (fwrite (copy_buf, 1, read_bytes, out_fp) != read_bytes)
			{
				status = ERR_CANT_CREATE;
				break;
			}
			rem -= read_bytes;
		}
		fclose (in_fp);
		if (status != ERR_OK)
			break;

		if (is_hfs0)
		{
			u64 pad = ((files[i].size + 511ULL) & ~511ULL) - files[i].size;
			if (pad > 0)
			{
				u8 zeros[512] = { 0 };
				fwrite (zeros, 1, (size_t)pad, out_fp);
			}
		}
	}

	FREE (copy_buf);
	FREE (files);
	fclose (out_fp);
	return status;
}

