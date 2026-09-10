// SPDX-License-Identifier: GPL-2.0+
// Split out of lib-nintendo-archives.c -- one archive format per file.
#include "lib-nintendo-archives.h"
#include "lib-nintendo.h"
#include "lib-image.h"
#include "lib-camelot.h"
#include "lib-yay0.h"
#include "lib-flim.h"
#include "lib-szs.h"
#include "lib-std.h"
#include "lib-zstd.h"
#include "lib-archive-util.h"
#include <zlib.h>
#include <stdlib.h>
#include <string.h>


// ----------------------------------------------------------------------------
// 13. PlatinumGames Archive (.pkz / pkz)
// ----------------------------------------------------------------------------
enumError ExtractPKZArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".pkz") && !is_ext_match (arg, ".bin") && !is_ext_match (arg, ".dat"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 32 || memcmp (raw, "pkz\0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 file_count = rd_le32 (raw + 16);
	const u32 offset_file_info = rd_le32 (raw + 20);

	if (!file_count || file_count > 100000 || offset_file_info >= raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	const uint info_size = file_count * 32;
	const uint str_table_pos = offset_file_info + info_size;
	if (str_table_pos > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PKZ:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, file_count, dest);

	for (uint i = 0; i < file_count; i++)
	{
		const uint entry_pos = offset_file_info + i * 32;
		if (entry_pos + 32 > raw_size)
			break;

		const u64 name_offset
			= (u64)rd_le32 (raw + entry_pos) | ((u64)rd_le32 (raw + entry_pos + 4) << 32);
		const u64 file_size
			= (u64)rd_le32 (raw + entry_pos + 8) | ((u64)rd_le32 (raw + entry_pos + 12) << 32);
		const u64 file_offset
			= (u64)rd_le32 (raw + entry_pos + 16) | ((u64)rd_le32 (raw + entry_pos + 20) << 32);
		const u64 comp_size
			= (u64)rd_le32 (raw + entry_pos + 24) | ((u64)rd_le32 (raw + entry_pos + 28) << 32);

		char name[PATH_MAX];
		const uint full_name_pos = str_table_pos + (uint)name_offset;
		if (full_name_pos < raw_size)
		{
			const char *s = (const char *)(raw + full_name_pos);
			size_t slen = strnlen (s, sizeof (name) - 1);
			memcpy (name, s, slen);
			name[slen] = 0;
		}
		else
		{
			snprintf (name, sizeof (name), "file_%04u.bin", i);
		}

		if (file_offset >= raw_size)
			continue;

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

		char *slash = strrchr (out_path, '/');
		if (slash)
		{
			*slash = 0;
			CreatePath (out_path, true);
			*slash = '/';
		}

		if (!testmode && comp_size > 0 && (size_t)(file_offset + comp_size) <= raw_size)
		{
			if (comp_size != file_size && file_size > 0)
			{
				// Zstandard decompression (Zstb)
				u8 *decomp = 0;
				uint decomp_sz = 0;
				err = DecodeZSTD (&decomp, &decomp_sz, raw + file_offset, (uint)comp_size);
				if (!err && decomp)
				{
					SaveFile (out_path, 0, 0, decomp, decomp_sz, 0);
					FREE (decomp);
				}
				else
				{
					SaveFile (out_path, 0, 0, raw + file_offset, (uint)comp_size, 0);
				}
			}
			else
			{
				SaveFile (out_path, 0, 0, raw + file_offset, (uint)comp_size, 0);
			}
		}
	}

	FREE (raw);
	return ERR_OK;
}


// PlatinumGames Archive (.pkz), little-endian, members stored uncompressed
enumError CreatePKZArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	const u32 offset_file_info = 32;
	const u32 str_table_pos = offset_file_info + n_entries * 32;

	u32 names_len = 0;
	for (uint i = 0; i < n_entries; i++)
		names_len += (u32)strlen (leaf_name (sorted[i].name)) + 1;

	const u32 data_start = align_up (str_table_pos + names_len, 16);
	u32 total = data_start;
	for (uint i = 0; i < n_entries; i++)
		total = align_up (total + sorted[i].size, 16);

	u8 *buf = CALLOC (total, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (buf, "pkz\0", 4);
	wr_le32 (buf + 16, n_entries);
	wr_le32 (buf + 20, offset_file_info);

	u32 name_rel = 0;
	u32 data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = leaf_name (sorted[i].name);
		const size_t nlen = strlen (name);
		memcpy (buf + str_table_pos + name_rel, name, nlen + 1);

		const uint entry_pos = offset_file_info + i * 32;
		wr_le32 (buf + entry_pos, name_rel);
		wr_le32 (buf + entry_pos + 8, sorted[i].size);
		wr_le32 (buf + entry_pos + 16, data_off);
		wr_le32 (buf + entry_pos + 24, sorted[i].size); // stored size == raw size

		if (sorted[i].data && sorted[i].size)
			memcpy (buf + data_off, sorted[i].data, sorted[i].size);
		data_off = align_up (data_off + sorted[i].size, 16);
		name_rel += (u32)nlen + 1;
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = total;
	return ERR_OK;
}
