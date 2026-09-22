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
		if ((uint64_t)off < table_sz || (uint64_t)off + 0x28 + len > raw_size || off < prev_off)
		{
			FREE (raw);
			return ERR_INVALID_DATA;
		}
		prev_off = off;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	if (!testmode)
		CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PVOL:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, fcount - 1, dest);

	for (uint i = 0; i < fcount - 1; i++)
	{
		const u32 toff = 4 + i * 8;
		const u32 off = rd_le32 (raw + toff);
		const u32 len = rd_le32 (raw + toff + 4);

		// The two name fields are fixed-width byte strings, not necessarily
		// terminated. Never scan into the payload when reading either field.
		char name1[33] = {0};
		char name2[9] = {0};
		memcpy (name1, raw + off, 32);
		memcpy (name2, raw + off + 32, 8);
		char full_name[80];
		snprintf (full_name, sizeof (full_name), "%s%s", name1, name2);
		if (!OwnedNameOk (full_name))
			snprintf (full_name, sizeof (full_name), "file_%04u.bin", i);

		const u32 data_off = off + 0x28;

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, full_name);

		if (!testmode)
		{
			err = SaveFile (out_path, 0, 0, raw + data_off, len, 0);
			if (err)
				break;
		}
	}

	FREE (raw);
	return err;
}


static int compare_pvol_entries (const void *a, const void *b)
{
	const nintendo_sarc_entry_t *ea = a, *eb = b;
	return strcmp (leaf_name (ea->name), leaf_name (eb->name));
}

// 4. Pikmin 1 & 2 Model/Archive Container (.pvol)
enumError CreatePVOLArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size)
		return ERR_INVALID_DATA;
	*dest = 0;
	*dest_size = 0;
	if (!entries || !n_entries || n_entries > 99999)
		return ERR_INVALID_DATA;

	const uint data_start = (4 + n_entries * 8 + 31) & ~31u;
	u64 total_size = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		const ccp name = leaf_name (entries[i].name);
		if (!OwnedNameOk (name) || strlen (name) > 40 || (entries[i].size && !entries[i].data))
			return ERR_INVALID_DATA;
		total_size = (total_size + 0x28 + entries[i].size + 15) & ~(u64)15;
		if (total_size > UINT_MAX)
			return ERR_INVALID_DATA;
	}

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_pvol_entries);
	for (uint i = 1; i < n_entries; i++)
		if (!compare_pvol_entries (sorted + i - 1, sorted + i))
		{
			FREE (sorted);
			return ERR_INVALID_DATA;
		}

	const u32 fcount = n_entries + 1;
	const uint cur_off = (uint)total_size;

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


enumError create_pvol_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreatePVOLArchive (&data, &size, list.entry, list.used);
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

