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

	// Validate every range before writing anything. Adding in 32 bits can
	// wrap, and clamping a truncated entry silently loses its data.
	for (uint i = 0; i < count; i++)
	{
		const u8 *entry = raw + 8 + i * 16;
		if ((u64)rd_be32 (entry + 4) + rd_be32 (entry + 8) > raw_size)
		{
			FREE (raw);
			return ERR_INVALID_DATA;
		}
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	if (!testmode)
		CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ZTAB:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);

	for (uint i = 0; i < count; i++)
	{
		const u32 eoff = 8 + i * 16;
		const u32 flags = rd_be32 (raw + eoff);
		const u32 off = rd_be32 (raw + eoff + 4);
		const u32 sz = rd_be32 (raw + eoff + 8);

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/entry_%04u_flags_%08x.bin", dest, i, flags);

		if (!testmode)
		{
			err = SaveFile (out_path, 0, 0, raw + off, sz, 0);
			if (err)
				break;
		}
	}

	FREE (raw);
	return err;
}

static bool ztab_entry_index (ccp name, ulong *index)
{
	name = leaf_name (name);
	if (strncmp (name, "entry_", 6) || name[6] < '0' || name[6] > '9')
		return false;
	char *end = 0;
	*index = strtoul (name + 6, &end, 10);
	return *index <= UINT_MAX && !strncmp (end, "_flags_", 7);
}

static int compare_ztab_entries (const void *a, const void *b)
{
	const nintendo_sarc_entry_t *ea = a, *eb = b;
	ulong ia = 0, ib = 0;
	const bool indexed_a = ztab_entry_index (ea->name, &ia);
	const bool indexed_b = ztab_entry_index (eb->name, &ib);
	if (indexed_a != indexed_b)
		return indexed_a ? -1 : 1;
	if (indexed_a && ia != ib)
		return ia < ib ? -1 : 1;
	return compare_archive_entries (a, b);
}

// 2. Camelot Archive Table (.ztab / .tab)
enumError CreateZTABArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size)
		return ERR_INVALID_DATA;
	*dest = 0;
	*dest_size = 0;
	if (!entries || !n_entries || n_entries > 100000)
		return ERR_INVALID_DATA;

	const uint data_start = (8 + n_entries * 16 + 15) & ~15u;
	u64 total_size = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		if (entries[i].size && !entries[i].data)
			return ERR_INVALID_DATA;
		total_size = (total_size + entries[i].size + 15) & ~(u64)15;
		if (total_size > UINT_MAX)
			return ERR_INVALID_DATA;
	}

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_ztab_entries);

	const u32 header_sz = 8;
	const uint cur_data_off = (uint)total_size;

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


enumError create_ztab_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateZTABArchive (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, dest);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}

