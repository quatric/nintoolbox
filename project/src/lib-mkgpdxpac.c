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
// 9. Mario Kart Arcade GP DX Layout Archive (.pac / pack)
// ----------------------------------------------------------------------------
enumError ExtractMKGPDXPacArchive (ccp arg, ccp basedir, uint depth)
{
	// .mkgpdx is the disambiguating extension CREATE writes, since .pac is
	// already claimed by the HAL Laboratory / Game Arts container.
	if (!is_ext_match (arg, ".pac") && !is_ext_match (arg, ".bin")
		&& !is_ext_match (arg, ".mkgpdx"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 20 || memcmp (raw, "pack", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 file_count = rd_le32 (raw + 4);
	const u32 str_pool_offset = rd_le32 (raw + 8);
	const u32 alignment = rd_le32 (raw + 12);
	const u32 file_type = rd_le32 (raw + 16);
	(void)file_type;

	if (!file_count || file_count > 100000)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	const u32 entries_size = 20 + file_count * 16;
	if (entries_size > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	const u32 align_val = alignment ? alignment : 1;
	const u32 data_block_pos = (entries_size + (align_val - 1)) & ~(align_val - 1);
	if (data_block_pos >= raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT MKAGPDX PAC:%s (%u files, align=%u) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, file_count, align_val, dest);

	for (uint i = 0; i < file_count; i++)
	{
		const uint entry_pos = 20 + i * 16;
		const u32 unknown = rd_le32 (raw + entry_pos);
		(void)unknown;
		const u32 name_offset = rd_le32 (raw + entry_pos + 4);
		const u32 offset = rd_le32 (raw + entry_pos + 8);
		u32 size = rd_le32 (raw + entry_pos + 12);

		const u32 name_pos = data_block_pos + str_pool_offset + name_offset;
		char name[PATH_MAX];
		if (name_pos < raw_size)
		{
			const char *s = (const char *)(raw + name_pos);
			size_t slen = strnlen (s, sizeof (name) - 1);
			memcpy (name, s, slen);
			name[slen] = 0;
		}
		else
		{
			snprintf (name, sizeof (name), "file_%04u.bin", i);
		}

		const u32 file_pos = data_block_pos + offset;
		if (file_pos + size > raw_size)
			size = raw_size > file_pos ? (u32)(raw_size - file_pos) : 0;

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

		char *slash = strrchr (out_path, '/');
		if (slash)
		{
			*slash = 0;
			CreatePath (out_path, true);
			*slash = '/';
		}

		if (!testmode && size > 0 && file_pos < raw_size)
			SaveFile (out_path, 0, 0, raw + file_pos, size, 0);
	}

	FREE (raw);
	return ERR_OK;
}


// Mario Kart Arcade GP DX layout archive ("pack"), little-endian.
//
// Offsets in the entry table are relative to the aligned data block rather
// than to the file, and the name pool lives inside that block too. The
// canonical layout below puts the pool at the head of the block
// (str_pool_offset 0) and aligns every member to 32 bytes.
//
// .pac is already claimed by the HAL Laboratory / Game Arts container, so
// CREATE selects this format on the .mkgpdx extension, the same way
// .sarcle and .at7p disambiguate their own overloaded extensions.
enumError CreateMKGPDXPacArchive (
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
	const u32 data_block_pos = align_up (20 + n_entries * 16, alignment);

	u32 names_len = 0;
	for (uint i = 0; i < n_entries; i++)
		names_len += (u32)strlen (leaf_name (sorted[i].name)) + 1;

	// Member offsets are relative to data_block_pos, and the name pool
	// occupies the start of the block.
	const u32 first_member_rel = align_up (names_len, alignment);
	u32 total_rel = first_member_rel;
	for (uint i = 0; i < n_entries; i++)
		total_rel = align_up (total_rel + sorted[i].size, alignment);

	const u32 total = data_block_pos + total_rel;
	u8 *buf = CALLOC (total, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (buf, "pack", 4);
	wr_le32 (buf + 4, n_entries);
	wr_le32 (buf + 8, 0); // string pool at the head of the data block
	wr_le32 (buf + 12, alignment);

	u32 name_rel = 0;
	u32 member_rel = first_member_rel;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = leaf_name (sorted[i].name);
		const size_t nlen = strlen (name);
		memcpy (buf + data_block_pos + name_rel, name, nlen + 1);

		const uint entry_pos = 20 + i * 16;
		wr_le32 (buf + entry_pos + 4, name_rel);
		wr_le32 (buf + entry_pos + 8, member_rel);
		wr_le32 (buf + entry_pos + 12, sorted[i].size);

		if (sorted[i].data && sorted[i].size)
			memcpy (buf + data_block_pos + member_rel, sorted[i].data, sorted[i].size);
		member_rel = align_up (member_rel + sorted[i].size, alignment);
		name_rel += (u32)nlen + 1;
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = total;
	return ERR_OK;
}
