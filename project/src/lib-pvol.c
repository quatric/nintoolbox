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
// 4. Pikmin 1 & 2 Model/Archive Container (.pvol)
// ----------------------------------------------------------------------------
enumError ExtractPVOLArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".pvol"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 12)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 fcount = rd_le32 (raw);
	if (fcount < 2 || fcount > 100000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const uint64_t table_sz = (uint64_t)4 + (uint64_t)(fcount - 1) * 8;
	if (table_sz > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 first_off = rd_le32 (raw + 4);
	if ((uint64_t)first_off < table_sz || (uint64_t)first_off >= raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u32 prev_off = first_off;
	for (uint i = 0; i < fcount - 1; i++)
	{
		const u32 toff = 4 + i * 8;
		const u32 off = rd_le32 (raw + toff);
		const u32 len = rd_le32 (raw + toff + 4);
		if ((uint64_t)off < table_sz || (uint64_t)off + len > raw_size || off < prev_off)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		prev_off = off;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PVOL:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, fcount - 1, dest);

	for (uint i = 0; i < fcount - 1; i++)
	{
		const u32 toff = 4 + i * 8;
		const u32 off = rd_le32 (raw + toff);
		u32 len = rd_le32 (raw + toff + 4);

		if (off >= raw_size)
			continue;

		char name1[33] = "";
		char name2[33] = "";
		if (off + 0x20 <= raw_size)
			StringCopyS (name1, sizeof (name1), (ccp)(raw + off));
		if (off + 0x28 <= raw_size)
			StringCopyS (name2, sizeof (name2), (ccp)(raw + off + 0x20));

		char full_name[80];
		if (name1[0] || name2[0])
			snprintf (full_name, sizeof (full_name), "%s%s", name1, name2);
		else
			snprintf (full_name, sizeof (full_name), "file_%04u.bin", i);

		const u32 data_off = off + 0x28;
		if (data_off + len > raw_size)
			len = raw_size > data_off ? (uint)(raw_size - data_off) : 0;

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, full_name);

		if (!testmode && len > 0 && data_off < raw_size)
			SaveFile (out_path, 0, 0, raw + data_off, len, 0);
	}

	FREE (raw);
	return ERR_OK;
}


// 4. Pikmin 1 & 2 Model/Archive Container (.pvol)
enumError CreatePVOLArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	const u32 fcount = n_entries + 1;
	const u32 table_sz = 4 + (fcount - 1) * 8;
	u32 data_start = (table_sz + 31) & ~31;

	u32 cur_off = data_start;
	for (uint i = 0; i < n_entries; i++)
		cur_off = (cur_off + 0x28 + sorted[i].size + 15) & ~15;

	u8 *buf = CALLOC (cur_off, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	wr_le32 (buf, fcount);

	u32 off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		const u32 toff = 4 + i * 8;
		wr_le32 (buf + toff, off);
		wr_le32 (buf + toff + 4, sorted[i].size);

		ccp name = sorted[i].name ? sorted[i].name : "";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;

		const size_t nlen = strlen (name);
		if (nlen <= 32)
			memcpy (buf + off, name, nlen);
		else
		{
			memcpy (buf + off, name, 32);
			const size_t rem = nlen - 32 < 8 ? nlen - 32 : 8;
			memcpy (buf + off + 0x20, name + 32, rem);
		}

		if (sorted[i].data && sorted[i].size > 0)
			memcpy (buf + off + 0x28, sorted[i].data, sorted[i].size);

		off = (off + 0x28 + sorted[i].size + 15) & ~15;
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = cur_off;
	return ERR_OK;
}
