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
// 17. Game Freak Pokemon Archive (.gfpak / GFLXPACK)
// ----------------------------------------------------------------------------
enumError ExtractGFPAKArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".gfpak") && !is_ext_match (arg, ".bin") && !is_ext_match (arg, ".pak"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 40 || memcmp (raw, "GFLXPACK", 8))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// GFLXPACK Header (little-endian):
	// 0x00: Magic ("GFLXPACK", 8 bytes)
	// 0x08: u64 dummy (often 0x10)
	// 0x10: u32 file_count
	// 0x14: u32 dummy (often 2)
	// 0x18: u64 info_offset
	// 0x20: u64 name_hash_table_offset
	const u32 file_count = rd_le32 (raw + 16);
	const u64 info_offset = rd_le64 (raw + 24);

	if (!file_count || file_count > 100000 || info_offset >= raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT GFPAK:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, file_count, dest);

	for (uint i = 0; i < file_count; i++)
	{
		const u64 entry_pos = info_offset + (u64)i * 24;
		if (entry_pos + 24 > raw_size)
			break;

		// Entry:
		// 0x00: u16 dummy
		// 0x02: u16 zip (1 = uncompressed/raw, 2 = lz4/zlib, 3 = oodle/other)
		// 0x04: u32 uncomp_size
		// 0x08: u32 comp_size
		// 0x0C: u32 dummy
		// 0x10: u64 offset
		const u16 zip = rd_le16 (raw + entry_pos + 2);
		const u32 uncomp_sz = rd_le32 (raw + entry_pos + 4);
		u32 comp_sz = rd_le32 (raw + entry_pos + 8);
		const u64 file_off = rd_le64 (raw + entry_pos + 16);

		if (file_off >= raw_size)
			continue;
		if (file_off + comp_sz > raw_size)
			comp_sz = (u32)(raw_size - file_off);

		char name[PATH_MAX];
		snprintf (name, sizeof (name), "file_%04u.bin", i);

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

		char *slash = strrchr (out_path, '/');
		if (slash)
		{
			*slash = 0;
			CreatePath (out_path, true);
			*slash = '/';
		}

		if (!testmode && comp_sz > 0)
		{
			if (zip != 1 && comp_sz != uncomp_sz && comp_sz >= 2 && raw[file_off] == 0x78)
			{
				u8 *decomp = 0;
				uint decomp_sz = 0;
				err = DecodeZlibGrow (&decomp, &decomp_sz, raw + file_off, comp_sz);
				if (!err && decomp)
				{
					SaveFile (out_path, 0, 0, decomp, decomp_sz, 0);
					FREE (decomp);
				}
				else
				{
					SaveFile (out_path, 0, 0, raw + file_off, comp_sz, 0);
				}
			}
			else
			{
				SaveFile (out_path, 0, 0, raw + file_off, comp_sz, 0);
			}
		}
	}

	FREE (raw);
	return ERR_OK;
}
