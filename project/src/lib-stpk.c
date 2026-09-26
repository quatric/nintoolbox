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
// 5. Jump Super Stars / Jump Ultimate Stars DS Archive (.srd / .stpk / STPK)
// ----------------------------------------------------------------------------
enumError ExtractSTPKArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".srd") && !is_ext_match (arg, ".stpk") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 0x10 || memcmp (raw, "STPK", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 resource_count = rd_be32 (raw + 8);
	if (!resource_count || resource_count > 100000
		|| (uint64_t)0x10 + (uint64_t)resource_count * 0x30 > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT STPK:%s (%u resources) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, resource_count, dest);

	for (uint i = 0; i < resource_count; i++)
	{
		const u32 eoff = 0x10 + i * 0x30;
		const u32 off = rd_be32 (raw + eoff);
		u32 sz = rd_be32 (raw + eoff + 4);

		char slot[33] = { 0 };
		memcpy (slot, raw + eoff + 0x10, 32);
		slot[32] = 0;

		char name[64];
		if (!slot[0] || !OwnedNameOk (slot))
			snprintf (name, sizeof (name), "res_%04u.bin", i);
		else
			snprintf (name, sizeof (name), "%s", slot);

		if (off >= raw_size)
			continue;
		if ((u64)off + sz > raw_size)
			sz = (u32)(raw_size - off);

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

		if (!testmode && sz > 0)
			SaveFile (out_path, 0, 0, raw + off, sz, 0);
	}

	FREE (raw);
	return ERR_OK;
}

// 5. Jump Super Stars / Jump Ultimate Stars DS Archive (.srd / .stpk)
enumError CreateSTPKArchive (
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
		ccp name = sorted[i].name ? sorted[i].name : "";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;
		if (!OwnedNameOk (name))
		{
			FREE (sorted);
			return ERR_INVALID_DATA;
		}
	}

	const u32 header_sz = 0x10;
	const u32 table_sz = n_entries * 0x30;
	u32 data_start = (header_sz + table_sz + 15) & ~15;

	u64 cur_data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
		cur_data_off = (cur_data_off + sorted[i].size + 15) & ~15ull;

	if (cur_data_off > 0xFFFFFFFFull)
	{
		FREE (sorted);
		return EFBIG;
	}

	u8 *buf = CALLOC ((size_t)cur_data_off, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (buf, "STPK", 4);
	wr_be32 (buf + 4, 1);
	wr_be32 (buf + 8, n_entries);
	wr_be32 (buf + 12, 0);

	u32 data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		const u32 eoff = header_sz + i * 0x30;
		wr_be32 (buf + eoff, data_off);
		wr_be32 (buf + eoff + 4, sorted[i].size);

		ccp name = sorted[i].name ? sorted[i].name : "";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;
		strncpy ((char *)(buf + eoff + 0x10), name, 31);

		if (sorted[i].data && sorted[i].size > 0)
			memcpy (buf + data_off, sorted[i].data, sorted[i].size);

		data_off = (data_off + sorted[i].size + 15) & ~15;
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = (uint)cur_data_off;
	return ERR_OK;
}

enumError create_stpk_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateSTPKArchive (&data, &size, list.entry, list.used);
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
