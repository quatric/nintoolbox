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
// 12. Nintendo APAK Archive (.apak / APAK)
// ----------------------------------------------------------------------------
enumError ExtractAPAKArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".apak") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 24 || memcmp (raw, "APAK", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Detect endianness: version at offset 6 is 5
	const u16 ver_be = rd_be16 (raw + 6);
	const bool big = (ver_be == 5);

	const u32 file_count = big ? rd_be32 (raw + 8) : rd_le32 (raw + 8);
	const u32 unknown1 = big ? rd_be32 (raw + 12) : rd_le32 (raw + 12);
	const u32 file_info_size = big ? rd_be32 (raw + 16) : rd_le32 (raw + 16);
	(void)unknown1;
	(void)file_info_size;

	if (!file_count || file_count > 100000 || 24 + (uint64_t)file_count * 64 > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT APAK:%s (%u files, %s-endian) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, file_count,
			big ? "big" : "little", dest);

	for (uint i = 0; i < file_count; i++)
	{
		const uint entry_pos = 24 + i * 64;
		const u32 data_offset = big ? rd_be32 (raw + entry_pos + 4) : rd_le32 (raw + entry_pos + 4);
		u32 file_size = big ? rd_be32 (raw + entry_pos + 8) : rd_le32 (raw + entry_pos + 8);

		char name[33];
		memcpy (name, raw + entry_pos + 32, 32);
		name[32] = 0;

		if (!name[0])
			snprintf (name, sizeof (name), "file_%04u.bin", i);

		if (data_offset >= raw_size)
			continue;
		if (data_offset + file_size > raw_size)
			file_size = (u32)(raw_size - data_offset);

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

		char *slash = strrchr (out_path, '/');
		if (slash)
		{
			*slash = 0;
			CreatePath (out_path, true);
			*slash = '/';
		}

		if (!testmode && file_size > 0)
			SaveFile (out_path, 0, 0, raw + data_offset, file_size, 0);
	}

	FREE (raw);
	return ERR_OK;
}


// Nintendo APAK Archive (.apak / APAK), big-endian, version 5
enumError CreateAPAKArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	const u32 file_info_size = n_entries * 64;
	const u32 data_start = align_up (24 + file_info_size, 32);

	u32 total = data_start;
	for (uint i = 0; i < n_entries; i++)
		total = align_up (total + sorted[i].size, 32);

	u8 *buf = CALLOC (total, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (buf, "APAK", 4);
	wr_be16 (buf + 6, 5);
	wr_be32 (buf + 8, n_entries);
	wr_be32 (buf + 16, file_info_size);

	u32 data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		const uint entry_pos = 24 + i * 64;
		wr_be32 (buf + entry_pos + 4, data_off);
		wr_be32 (buf + entry_pos + 8, sorted[i].size);

		ccp name = leaf_name (sorted[i].name);
		strncpy ((char *)buf + entry_pos + 32, name, 32);

		if (sorted[i].data && sorted[i].size)
			memcpy (buf + data_off, sorted[i].data, sorted[i].size);
		data_off = align_up (data_off + sorted[i].size, 32);
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = total;
	return ERR_OK;
}
