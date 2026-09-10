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
// 10. Twilight Princess HD / Zelda TMPK Archive (.pack / TMPK)
// ----------------------------------------------------------------------------
enumError ExtractTMPKArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".pack") && !is_ext_match (arg, ".bin") && !is_ext_match (arg, ".tmpk"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 16 || memcmp (raw, "TMPK", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 file_count = rd_be32 (raw + 4);
	const u32 alignment = rd_be32 (raw + 8);

	if (!file_count || file_count > 100000 || 16 + (uint64_t)file_count * 16 > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT TMPK:%s (%u files, align=%u) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, file_count, alignment, dest);

	for (uint i = 0; i < file_count; i++)
	{
		const uint entry_pos = 16 + i * 16;
		const u32 name_offset = rd_be32 (raw + entry_pos);
		const u32 file_offset = rd_be32 (raw + entry_pos + 4);
		u32 file_size = rd_be32 (raw + entry_pos + 8);

		char name[PATH_MAX];
		if (name_offset < raw_size)
		{
			const char *s = (const char *)(raw + name_offset);
			size_t slen = strnlen (s, sizeof (name) - 1);
			memcpy (name, s, slen);
			name[slen] = 0;
		}
		else
		{
			snprintf (name, sizeof (name), "file_%04u.bin", i);
		}

		if (file_offset + file_size > raw_size)
			file_size = raw_size > file_offset ? (u32)(raw_size - file_offset) : 0;

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

		char *slash = strrchr (out_path, '/');
		if (slash)
		{
			*slash = 0;
			CreatePath (out_path, true);
			*slash = '/';
		}

		if (!testmode && file_size > 0 && file_offset < raw_size)
			SaveFile (out_path, 0, 0, raw + file_offset, file_size, 0);
	}

	FREE (raw);
	return ERR_OK;
}


// Twilight Princess HD Archive (.pack / TMPK), big-endian
enumError CreateTMPKArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	const u32 alignment = 32;
	const u32 names_start = 16 + n_entries * 16;

	u32 names_len = 0;
	for (uint i = 0; i < n_entries; i++)
		names_len += (u32)strlen (leaf_name (sorted[i].name)) + 1;

	const u32 data_start = align_up (names_start + names_len, alignment);
	u32 total = data_start;
	for (uint i = 0; i < n_entries; i++)
		total = align_up (total + sorted[i].size, alignment);

	u8 *buf = CALLOC (total, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (buf, "TMPK", 4);
	wr_be32 (buf + 4, n_entries);
	wr_be32 (buf + 8, alignment);

	u32 name_pos = names_start;
	u32 data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = leaf_name (sorted[i].name);
		const size_t nlen = strlen (name);
		memcpy (buf + name_pos, name, nlen + 1);

		const uint entry_pos = 16 + i * 16;
		wr_be32 (buf + entry_pos, name_pos);
		wr_be32 (buf + entry_pos + 4, data_off);
		wr_be32 (buf + entry_pos + 8, sorted[i].size);

		if (sorted[i].data && sorted[i].size)
			memcpy (buf + data_off, sorted[i].data, sorted[i].size);
		data_off = align_up (data_off + sorted[i].size, alignment);
		name_pos += (u32)nlen + 1;
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = total;
	return ERR_OK;
}
