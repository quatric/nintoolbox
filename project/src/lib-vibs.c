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
// 14. Nintendo Switch Joy-Con Vibration Archive (.vibs)
// ----------------------------------------------------------------------------
enumError ExtractVIBSArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".vibs"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 8)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 version = rd_le32 (raw);
	const u32 num_entries = rd_le32 (raw + 4);
	(void)version;

	if (!num_entries || num_entries > 100000 || 8 + (uint64_t)num_entries * 44 > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT VIBS:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, num_entries, dest);

	for (uint i = 0; i < num_entries; i++)
	{
		const uint entry_pos = 8 + i * 44;
		char name[25];
		memcpy (name, raw + entry_pos, 24);
		name[24] = 0;

		if (!name[0] || !OwnedNameOk (name))
			snprintf (name, sizeof (name), "vibration_%04u.bnvib", i);

		u32 data_len = rd_le32 (raw + entry_pos + 32);
		const u32 data_offset = rd_le32 (raw + entry_pos + 40);

		if (data_offset >= raw_size)
			continue;
		if ((u64)data_offset + data_len > raw_size)
			data_len = (u32)(raw_size - data_offset);

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

		char *slash = strrchr (out_path, '/');
		if (slash)
		{
			*slash = 0;
			CreatePath (out_path, true);
			*slash = '/';
		}

		if (!testmode && data_len > 0)
			SaveFile (out_path, 0, 0, raw + data_offset, data_len, 0);
	}

	FREE (raw);
	return ERR_OK;
}


// Nintendo Switch Joy-Con Vibration Archive (.vibs), little-endian
enumError CreateVIBSArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 100000)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	for (uint i = 0; i < n_entries; i++)
	{
		if (!OwnedNameOk (leaf_name (sorted[i].name)))
		{
			FREE (sorted);
			return ERR_INVALID_DATA;
		}
	}

	const u32 data_start = align_up (8 + n_entries * 44, 16);
	u64 total = data_start;
	for (uint i = 0; i < n_entries; i++)
		total = align_up (total + sorted[i].size, 16);

	if (total > 0xFFFFFFFFull)
	{
		FREE (sorted);
		return EFBIG;
	}

	u8 *buf = CALLOC ((size_t)total, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	wr_le32 (buf, 1); // version
	wr_le32 (buf + 4, n_entries);

	u32 data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		const uint entry_pos = 8 + i * 44;
		strncpy ((char *)buf + entry_pos, leaf_name (sorted[i].name), 24);
		wr_le32 (buf + entry_pos + 32, sorted[i].size);
		wr_le32 (buf + entry_pos + 40, data_off);

		if (sorted[i].data && sorted[i].size)
			memcpy (buf + data_off, sorted[i].data, sorted[i].size);
		data_off = align_up (data_off + sorted[i].size, 16);
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = (uint)total;
	return ERR_OK;
}


enumError create_vibs_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateVIBSArchive (&data, &size, list.entry, list.used);
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

