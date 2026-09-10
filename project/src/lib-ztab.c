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
// 2. Camelot Archive Table (.ztab / ZTAB)
// ----------------------------------------------------------------------------
enumError ExtractZTABArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".ztab") && !is_ext_match (arg, ".tab") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 8 || memcmp (raw, "ZTAB", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 count = rd_be32 (raw + 4);
	if (!count || count > 100000 || (uint64_t)8 + (uint64_t)count * 16 > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ZTAB:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);

	for (uint i = 0; i < count; i++)
	{
		const u32 eoff = 8 + i * 16;
		const u32 flags = rd_be32 (raw + eoff);
		const u32 off = rd_be32 (raw + eoff + 4);
		u32 sz = rd_be32 (raw + eoff + 8);

		if (off + sz > raw_size)
			sz = raw_size > off ? (uint)(raw_size - off) : 0;

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/entry_%04u_flags_%08x.bin", dest, i, flags);

		if (!testmode && sz > 0 && off < raw_size)
			SaveFile (out_path, 0, 0, raw + off, sz, 0);
	}

	FREE (raw);
	return ERR_OK;
}


// 2. Camelot Archive Table (.ztab / .tab)
enumError CreateZTABArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	const u32 header_sz = 8;
	const u32 table_sz = n_entries * 16;
	u32 data_start = (header_sz + table_sz + 15) & ~15;

	u32 cur_data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
		cur_data_off = (cur_data_off + sorted[i].size + 15) & ~15;

	u8 *buf = CALLOC (cur_data_off, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (buf, "ZTAB", 4);
	wr_be32 (buf + 4, n_entries);

	u32 data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		const u32 eoff = header_sz + i * 16;
		u32 flags = 0;
		ccp name = sorted[i].name ? sorted[i].name : "";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;

		const char *fpos = strstr (name, "flags_");
		if (fpos)
			sscanf (fpos + 6, "%x", &flags);

		wr_be32 (buf + eoff, flags);
		wr_be32 (buf + eoff + 4, data_off);
		wr_be32 (buf + eoff + 8, sorted[i].size);
		wr_be32 (buf + eoff + 12, 0);

		if (sorted[i].data && sorted[i].size > 0)
			memcpy (buf + data_off, sorted[i].data, sorted[i].size);

		data_off = (data_off + sorted[i].size + 15) & ~15;
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = cur_data_off;
	return ERR_OK;
}
