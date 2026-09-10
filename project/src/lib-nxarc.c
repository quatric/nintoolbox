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
// 11. Nintendo Switch NX Archive (.nxarc / RAXN)
// ----------------------------------------------------------------------------
enumError ExtractNXARCArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".nxarc") && !is_ext_match (arg, ".bin") && !is_ext_match (arg, ".arc"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 32 || memcmp (raw, "RAXN", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 offset_block = rd_le32 (raw + 12);
	const u32 header_size = rd_le32 (raw + 16);
	const u32 file_count = rd_le32 (raw + 20);
	const u32 block_size = rd_le32 (raw + 24);

	if (!file_count || file_count > 100000 || offset_block >= raw_size || header_size >= raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NXARC:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, file_count, dest);

	// Read zero-terminated strings from offset_block
	// File 0 is string table entry, actual files start at index 1
	uint str_pos = offset_block;
	for (uint i = 0; i < file_count; i++)
	{
		char name[PATH_MAX];
		if (str_pos < raw_size)
		{
			const char *s = (const char *)(raw + str_pos);
			size_t slen = strnlen (s, sizeof (name) - 1);
			memcpy (name, s, slen);
			name[slen] = 0;
			str_pos += (uint)slen + 1;
		}
		else
		{
			snprintf (name, sizeof (name), "file_%04u.bin", i);
		}

		if (i == 0)
			continue; // skip string table pseudo-entry

		const uint entry_pos = header_size + i * 32;
		if (entry_pos + 32 > raw_size)
			break;

		(void)block_size;
		const u64 size
			= (u64)rd_le32 (raw + entry_pos) | ((u64)rd_le32 (raw + entry_pos + 4) << 32);
		const u64 offset
			= (u64)rd_le32 (raw + entry_pos + 8) | ((u64)rd_le32 (raw + entry_pos + 12) << 32);
		const u64 flag
			= (u64)rd_le32 (raw + entry_pos + 16) | ((u64)rd_le32 (raw + entry_pos + 20) << 32);

		if (offset >= raw_size || (size_t)size > raw_size - offset)
			continue;

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

		char *slash = strrchr (out_path, '/');
		if (slash)
		{
			*slash = 0;
			CreatePath (out_path, true);
			*slash = '/';
		}

		if (!testmode && size > 0)
		{
			if (flag == 1)
			{
				// Zlib compressed
				u8 *decomp = 0;
				uint decomp_sz = 0;
				err = DecodeZlibGrow (&decomp, &decomp_sz, raw + offset, (uint)size);
				if (!err && decomp)
				{
					SaveFile (out_path, 0, 0, decomp, decomp_sz, 0);
					FREE (decomp);
				}
			}
			else
			{
				SaveFile (out_path, 0, 0, raw + offset, (uint)size, 0);
			}
		}
	}

	FREE (raw);
	return ERR_OK;
}


// Nintendo Switch NX Archive (.nxarc / RAXN), little-endian
enumError CreateNXARCArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	// Index 0 is the string-table pseudo entry the reader skips.
	const u32 file_count = n_entries + 1;
	const u32 header_size = 32;
	const u32 offset_block = header_size + file_count * 32;

	u32 names_len = 1; // empty name for the pseudo entry
	for (uint i = 0; i < n_entries; i++)
		names_len += (u32)strlen (leaf_name (sorted[i].name)) + 1;

	const u32 data_start = align_up (offset_block + names_len, 16);
	u32 total = data_start;
	for (uint i = 0; i < n_entries; i++)
		total = align_up (total + sorted[i].size, 16);

	u8 *buf = CALLOC (total, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (buf, "RAXN", 4);
	wr_le32 (buf + 12, offset_block);
	wr_le32 (buf + 16, header_size);
	wr_le32 (buf + 20, file_count);

	u32 name_pos = offset_block + 1; // pseudo entry already wrote its terminator
	u32 data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = leaf_name (sorted[i].name);
		const size_t nlen = strlen (name);
		memcpy (buf + name_pos, name, nlen + 1);
		name_pos += (u32)nlen + 1;

		const uint entry_pos = header_size + (i + 1) * 32;
		wr_le32 (buf + entry_pos, sorted[i].size);
		wr_le32 (buf + entry_pos + 8, data_off);

		if (sorted[i].data && sorted[i].size)
			memcpy (buf + data_off, sorted[i].data, sorted[i].size);
		data_off = align_up (data_off + sorted[i].size, 16);
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = total;
	return ERR_OK;
}
