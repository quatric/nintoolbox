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
bool IsMDR (const u8 *data, uint size)
{
	if (!data || size < 8)
		return false;
	const u32 count = rd_be32 (data);
	const u64 table_end = 4 + (u64)count * 4;
	if (!count || count > 100000 || table_end > size)
		return false;
	for (uint i = 0; i < count; i++)
	{
		const u32 off = rd_be32 (data + 4 + i * 4);
		const u32 end = i + 1 < count ? rd_be32 (data + 8 + i * 4) : size;
		if (off < table_end || end > size || (u64)off + 16 > end)
			return false;
		if (rd_be32 (data + off) != rd_be32 (data + off + 8)
			|| (u64)off + 16 + rd_be32 (data + off + 12) > end)
			return false;
	}
	return true;
}

enumError ExtractMDRArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".mdr") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size > UINT_MAX || !IsMDR (raw, (uint)raw_size)
		|| (is_ext_match (arg, ".bin") && IsMPBINInflate (raw, (uint)raw_size)))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 count = rd_be32 (raw);

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
		if ((uint64_t)off + 16 > raw_size)
			continue;

		const u32 decom_sz = rd_be32 (raw + off);
		(void)decom_sz;
		const u32 flags = rd_be32 (raw + off + 4);
		const u32 comp_sz = rd_be32 (raw + off + 12);

		// All three operands are 32-bit on retail files.  Promote before
		// adding: a random payload can otherwise wrap this bounds check and be
		// misidentified as MDR, as World of Goo's master.pak member was.
		if ((uint64_t)off + 16 + comp_sz > raw_size)
			continue;

		if (!testmode)
		{
			u8 *decomp_data = 0;
			uint decomp_sz = 0;
			char out_path[PATH_MAX];
			if (comp_sz > 0
				&& DecodeZlibGrow (&decomp_data, &decomp_sz, raw + off + 16, comp_sz) == ERR_OK
				&& decomp_data)
			{
				snprintf (out_path, sizeof (out_path), "%s/chunk_%02u_flags_%08x_zlib.bin", dest, i, flags);
				err = SaveFile (out_path, 0, 0, decomp_data, decomp_sz, 0);
				FREE (decomp_data);
			}
			else
			{
				// Not valid zlib data -- the chunk is stored raw/uncompressed.
				// Marked "_raw" so CreateMDRArchive() can store it back verbatim
				// instead of zlib-compressing it, keeping retail files byte-exact.
				snprintf (out_path, sizeof (out_path), "%s/chunk_%02u_flags_%08x_raw.bin", dest, i, flags);
				err = SaveFile (out_path, 0, 0, raw + off + 16, comp_sz, 0);
			}
		}
		if (err)
			break;
	}

	FREE (raw);
	return err;
}


static bool mdr_chunk_index (ccp name, ulong *index)
{
	if (!name || strncmp (name, "chunk_", 6) || name[6] < '0' || name[6] > '9')
		return false;
	char *end = 0;
	*index = strtoul (name + 6, &end, 10);
	return *index <= UINT_MAX && !strncmp (end, "_flags_", 7);
}

static int compare_mdr_entries (const void *a, const void *b)
{
	const nintendo_sarc_entry_t *ea = a, *eb = b;
	ulong ia = 0, ib = 0;
	const bool numbered_a = mdr_chunk_index (ea->name, &ia);
	const bool numbered_b = mdr_chunk_index (eb->name, &ib);
	if (numbered_a != numbered_b)
		return numbered_a ? -1 : 1;
	if (numbered_a && ia != ib)
		return ia < ib ? -1 : 1;
	return compare_archive_entries (a, b);
}

// 3. Dance Dance Revolution Mario Mix Chunk Archive (.mdr)
enumError CreateMDRArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size)
		return ERR_INVALID_DATA;
	*dest = 0;
	*dest_size = 0;
	if (!entries || !n_entries || n_entries > 100000)
		return ERR_INVALID_DATA;

	const uint header_size = 4 + n_entries * 4;
	u64 minimum_size = header_size + (u64)n_entries * 16;
	for (uint i = 0; i < n_entries; i++)
	{
		if (entries[i].size && !entries[i].data)
			return ERR_INVALID_DATA;
		if (entries[i].name && strstr (entries[i].name, "_raw."))
			minimum_size += ((u64)entries[i].size + 1) & ~(u64)1;
		else
		{
			const uLong bound = compressBound (entries[i].size);
			if (bound < entries[i].size || bound > UINT_MAX)
				return ERR_INVALID_DATA;
		}
		if (minimum_size > UINT_MAX)
			return ERR_INVALID_DATA;
	}

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	u8 **compressed = CALLOC (n_entries, sizeof (*compressed));
	uint *stored_sizes = CALLOC (n_entries, sizeof (*stored_sizes));
	enumError err = ERR_OUT_OF_MEMORY;
	if (!sorted || !compressed || !stored_sizes)
		goto cleanup;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_mdr_entries);

	u64 total_size = header_size;
	for (uint i = 0; i < n_entries; i++)
	{
		const nintendo_sarc_entry_t *e = sorted + i;
		const bool is_raw = e->name && strstr (e->name, "_raw.");
		if (is_raw)
			stored_sizes[i] = e->size;
		else if (e->size)
		{
			uLongf capacity = compressBound (e->size);
			compressed[i] = MALLOC (capacity);
			if (!compressed[i])
				goto cleanup;
			if (compress (compressed[i], &capacity, e->data, e->size) != Z_OK)
			{
				err = ERR_CANT_CREATE;
				goto cleanup;
			}
			stored_sizes[i] = (uint)capacity;
		}
		total_size = (total_size + 16 + stored_sizes[i] + 1) & ~(u64)1;
		if (total_size > UINT_MAX)
		{
			err = ERR_INVALID_DATA;
			goto cleanup;
		}
	}

	u8 *buf = CALLOC ((size_t)total_size, 1);
	if (!buf)
		goto cleanup;
	wr_be32 (buf, n_entries);
	uint offset = header_size;
	for (uint i = 0; i < n_entries; i++)
	{
		const nintendo_sarc_entry_t *e = sorted + i;
		uint flags = 0;
		const char *fpos = e->name ? strstr (e->name, "flags_") : 0;
		if (fpos)
			sscanf (fpos + 6, "%x", &flags);
		wr_be32 (buf + 4 + i * 4, offset);
		wr_be32 (buf + offset, e->size);
		wr_be32 (buf + offset + 4, flags);
		wr_be32 (buf + offset + 8, e->size);
		wr_be32 (buf + offset + 12, stored_sizes[i]);
		if (stored_sizes[i])
			memcpy (buf + offset + 16, compressed[i] ? compressed[i] : e->data, stored_sizes[i]);
		// Retail chunks are aligned to two bytes, not sixteen.
		offset = (offset + 16 + stored_sizes[i] + 1) & ~1u;
	}
	*dest = buf;
	*dest_size = (uint)total_size;
	err = ERR_OK;

cleanup:
	if (compressed)
		for (uint i = 0; i < n_entries; i++)
			FREE (compressed[i]);
	FREE (compressed);
	FREE (stored_sizes);
	FREE (sorted);
	return err;
}

enumError create_mdr_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateMDRArchive (&data, &size, list.entry, list.used);
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

