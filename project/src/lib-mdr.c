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
// 3. Dance Dance Revolution Mario Mix Chunk Archive (.mdr)
// ----------------------------------------------------------------------------
enumError ExtractMDRArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".mdr"))
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

	const u32 count = rd_be32 (raw);
	if (!count || count > 100000 || (uint64_t)4 + (uint64_t)count * 4 > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Validate first offset
	const u32 first_off = rd_be32 (raw + 4);
	if ((uint64_t)first_off < (uint64_t)4 + (uint64_t)count * 4
		|| (uint64_t)first_off + 16 > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT MDR:%s (%u chunks) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);

	for (uint i = 0; i < count; i++)
	{
		const u32 chunk_ptr_off = 4 + i * 4;
		if (chunk_ptr_off + 4 > raw_size)
			break;
		const u32 off = rd_be32 (raw + chunk_ptr_off);
		if (off + 16 > raw_size)
			continue;

		const u32 decom_sz = rd_be32 (raw + off);
		(void)decom_sz;
		const u32 flags = rd_be32 (raw + off + 4);
		const u32 comp_sz = rd_be32 (raw + off + 12);

		if (off + 16 + comp_sz > raw_size)
			continue;

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/chunk_%02u_flags_%08x.bin", dest, i, flags);

		if (!testmode)
		{
			u8 *decomp_data = 0;
			uint decomp_sz = 0;
			if (comp_sz > 0
				&& DecodeZlibGrow (&decomp_data, &decomp_sz, raw + off + 16, comp_sz) == ERR_OK
				&& decomp_data)
			{
				SaveFile (out_path, 0, 0, decomp_data, decomp_sz, 0);
				FREE (decomp_data);
			}
			else if (comp_sz > 0)
			{
				SaveFile (out_path, 0, 0, raw + off + 16, comp_sz, 0);
			}
		}
	}

	FREE (raw);
	return ERR_OK;
}


// 3. Dance Dance Revolution Mario Mix Chunk Archive (.mdr)
enumError CreateMDRArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	u8 **comp_chunks = CALLOC (n_entries, sizeof (u8 *));
	u32 *comp_sizes = CALLOC (n_entries, sizeof (u32));
	u32 *flags = CALLOC (n_entries, sizeof (u32));

	if (!comp_chunks || !comp_sizes || !flags)
	{
		FREE (sorted);
		FREE (comp_chunks);
		FREE (comp_sizes);
		FREE (flags);
		return ERR_OUT_OF_MEMORY;
	}

	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = sorted[i].name ? sorted[i].name : "";
		const char *fpos = strstr (name, "flags_");
		if (fpos)
			sscanf (fpos + 6, "%x", &flags[i]);

		if (sorted[i].size > 0 && sorted[i].data)
		{
			uLongf bound = compressBound (sorted[i].size);
			comp_chunks[i] = MALLOC (bound);
			uLongf actual = bound;
			if (compress (comp_chunks[i], &actual, sorted[i].data, sorted[i].size) == Z_OK)
			{
				comp_sizes[i] = (u32)actual;
			}
			else
			{
				FREE (comp_chunks[i]);
				comp_chunks[i] = 0;
				comp_sizes[i] = 0;
			}
		}
	}

	const u32 header_sz = (4 + n_entries * 4 + 15) & ~15;
	u32 cur_off = header_sz;
	u32 *chunk_ptrs = CALLOC (n_entries, sizeof (u32));
	for (uint i = 0; i < n_entries; i++)
	{
		chunk_ptrs[i] = cur_off;
		cur_off = (cur_off + 16 + comp_sizes[i] + 15) & ~15;
	}

	u8 *buf = CALLOC (cur_off, 1);
	if (!buf)
	{
		for (uint i = 0; i < n_entries; i++)
			FREE (comp_chunks[i]);
		FREE (comp_chunks);
		FREE (comp_sizes);
		FREE (flags);
		FREE (chunk_ptrs);
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	wr_be32 (buf, n_entries);
	for (uint i = 0; i < n_entries; i++)
		wr_be32 (buf + 4 + i * 4, chunk_ptrs[i]);

	for (uint i = 0; i < n_entries; i++)
	{
		const u32 coff = chunk_ptrs[i];
		wr_be32 (buf + coff, sorted[i].size);
		wr_be32 (buf + coff + 4, flags[i]);
		wr_be32 (buf + coff + 8, 0);
		wr_be32 (buf + coff + 12, comp_sizes[i]);

		if (comp_chunks[i] && comp_sizes[i] > 0)
		{
			memcpy (buf + coff + 16, comp_chunks[i], comp_sizes[i]);
			FREE (comp_chunks[i]);
		}
	}

	FREE (comp_chunks);
	FREE (comp_sizes);
	FREE (flags);
	FREE (chunk_ptrs);
	FREE (sorted);

	*dest = buf;
	*dest_size = cur_off;
	return ERR_OK;
}
