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
// 7. NES Remix indieszero Archive (.zlarc)
// ----------------------------------------------------------------------------
enumError ExtractZLARCArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".zlarc") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *comp = 0;
	size_t comp_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &comp, &comp_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (comp_size < 16)
	{
		FREE (comp);
		return ERR_NOTHING_TO_DO;
	}

	u8 *raw = 0;
	uint raw_size = 0;
	err = DecodeZlibGrow (&raw, &raw_size, comp, comp_size);
	FREE (comp);

	if (err || !raw || raw_size < 16)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 count = rd_be32 (raw);
	if (!count || count > 100000 || (uint64_t)4 + (uint64_t)count * 4 > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Validate first offset
	const u32 first_off = rd_be32 (raw + 4);
	if ((uint64_t)first_off < (uint64_t)4 + (uint64_t)count * 4
		|| (uint64_t)first_off + 12 > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Calculate data_start: end of all descriptors
	u32 data_start = 0;
	for (uint i = 0; i < count; i++)
	{
		const u32 desc_off = rd_be32 (raw + 4 + i * 4);
		if ((u64)desc_off + 12 > raw_size)
			continue;
		const u32 name_len = rd_be32 (raw + desc_off + 8);
		const u64 meta_end = (u64)desc_off + 12 + name_len;
		if (meta_end <= raw_size && meta_end > data_start)
			data_start = (u32)meta_end;
	}

	if (data_start >= raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ZLARC:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);

	for (uint i = 0; i < count; i++)
	{
		const u32 desc_off = rd_be32 (raw + 4 + i * 4);
		if ((u64)desc_off + 12 > raw_size)
			continue;

		const u32 data_off = rd_be32 (raw + desc_off);
		u32 data_sz = rd_be32 (raw + desc_off + 4);
		const u32 name_len = rd_be32 (raw + desc_off + 8);

		char name[PATH_MAX];
		if ((u64)desc_off + 12 + name_len <= raw_size && name_len > 0)
		{
			uint cpy = name_len < sizeof (name) - 1 ? name_len : (uint)sizeof (name) - 1;
			memcpy (name, raw + desc_off + 12, cpy);
			name[cpy] = 0;
			while (cpy > 0 && name[cpy - 1] == 0)
				name[--cpy] = 0;
			if (!name[0] || !OwnedNameOk (name))
				snprintf (name, sizeof (name), "file_%04u.bin", i);
		}
		else
		{
			snprintf (name, sizeof (name), "file_%04u.bin", i);
		}

		const u64 file_off64 = (u64)data_start + data_off;
		if (file_off64 >= raw_size)
			continue;
		const u32 file_off = (u32)file_off64;
		if (file_off64 + data_sz > raw_size)
			data_sz = (u32)(raw_size - file_off64);

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

		char *slash = strrchr (out_path, '/');
		if (slash)
		{
			*slash = 0;
			CreatePath (out_path, true);
			*slash = '/';
		}

		if (!testmode && data_sz > 0)
			SaveFile (out_path, 0, 0, raw + file_off, data_sz, 0);
	}

	FREE (raw);
	return ERR_OK;
}

enumError CreateZLARCArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 100000)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	const u32 header_sz = 4 + n_entries * 4;
	u64 descriptors_sz = 0;
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
		descriptors_sz += 12 + strlen (name) + 1;
	}

	const u64 data_start = (u64)header_sz + descriptors_sz;
	u64 total_data_sz = 0;
	for (uint i = 0; i < n_entries; i++)
		total_data_sz += sorted[i].size;

	const u64 uncomp_sz = data_start + total_data_sz;
	if (uncomp_sz > 0xFFFFFFFFull)
	{
		FREE (sorted);
		return EFBIG;
	}

	u8 *uncomp = CALLOC ((size_t)uncomp_sz, 1);
	if (!uncomp)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	wr_be32 (uncomp, n_entries);

	u32 cur_desc_off = header_sz;
	u32 cur_data_off = 0;

	for (uint i = 0; i < n_entries; i++)
	{
		wr_be32 (uncomp + 4 + i * 4, cur_desc_off);

		ccp name = sorted[i].name ? sorted[i].name : "";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;
		const uint nlen = (uint)strlen (name) + 1;

		wr_be32 (uncomp + cur_desc_off, cur_data_off);
		wr_be32 (uncomp + cur_desc_off + 4, sorted[i].size);
		wr_be32 (uncomp + cur_desc_off + 8, nlen);
		memcpy (uncomp + cur_desc_off + 12, name, nlen);

		if (sorted[i].data && sorted[i].size > 0)
			memcpy (uncomp + (size_t)data_start + cur_data_off, sorted[i].data, sorted[i].size);

		cur_desc_off += 12 + nlen;
		cur_data_off += sorted[i].size;
	}

	FREE (sorted);

	uLongf bound = compressBound ((uLong)uncomp_sz);
	u8 *comp = MALLOC (bound);
	if (!comp)
	{
		FREE (uncomp);
		return ERR_OUT_OF_MEMORY;
	}
	uLongf comp_len = bound;
	if (compress (comp, &comp_len, uncomp, (uLong)uncomp_sz) != Z_OK)
	{
		FREE (uncomp);
		FREE (comp);
		return ERR_CANT_CREATE;
	}

	FREE (uncomp);
	*dest = comp;
	*dest_size = (uint)comp_len;
	return ERR_OK;
}

enumError create_zlarc_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateZLARCArchive (&data, &size, list.entry, list.used);
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
