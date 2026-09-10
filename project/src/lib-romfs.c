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


// Extract Nintendo 3DS RomFS Archive (.romfs / IVFC)
static void RomFS_ReadDirectories (const u8 *raw, size_t raw_size, uint dir_start, uint file_start,
	uint data_start, uint cur_dir_off, ccp current_path, ccp dest)
{
	if (dir_start + cur_dir_off + 24 > raw_size)
		return;

	const u8 *dir = raw + dir_start + cur_dir_off;
	const u32 parent_off = rd_le32 (dir);
	const u32 next_sibling_off = rd_le32 (dir + 4);
	const u32 first_child_off = rd_le32 (dir + 8);
	const u32 first_file_off = rd_le32 (dir + 12);
	const u32 next_dir_bucket = rd_le32 (dir + 16);
	const u32 name_len = rd_le32 (dir + 20);

	char dir_name[PATH_MAX] = "";
	if (name_len > 0 && dir_start + cur_dir_off + 24 + name_len <= raw_size)
	{
		// UTF-16LE to ASCII
		const u8 *nptr = dir + 24;
		uint nidx = 0;
		for (uint i = 0; i < name_len; i += 2)
		{
			u16 ch = rd_le16 (nptr + i);
			if (ch == 0)
				break;
			dir_name[nidx++] = (ch < 128) ? (char)ch : '_';
			if (nidx >= sizeof (dir_name) - 1)
				break;
		}
		dir_name[nidx] = 0;
	}

	char new_path[PATH_MAX];
	if (*dir_name)
		snprintf (new_path, sizeof (new_path), "%s%s/", current_path, dir_name);
	else
		snprintf (new_path, sizeof (new_path), "%s", current_path);

	// Read files in this directory
	if (first_file_off != 0xFFFFFFFF && file_start + first_file_off < raw_size)
	{
		u32 cur_file_off = first_file_off;
		while (cur_file_off != 0xFFFFFFFF && file_start + cur_file_off + 32 <= raw_size)
		{
			const u8 *file = raw + file_start + cur_file_off;
			const u32 next_file_sib = rd_le32 (file + 4);
			const u64 f_data_off = rd_le64 (file + 8);
			const u64 f_data_sz = rd_le64 (file + 16);
			const u32 f_name_len = rd_le32 (file + 28);

			char fname[PATH_MAX] = "";
			if (f_name_len > 0 && file_start + cur_file_off + 32 + f_name_len <= raw_size)
			{
				const u8 *fnptr = file + 32;
				uint fnidx = 0;
				for (uint i = 0; i < f_name_len; i += 2)
				{
					u16 ch = rd_le16 (fnptr + i);
					if (ch == 0)
						break;
					fname[fnidx++] = (ch < 128) ? (char)ch : '_';
					if (fnidx >= sizeof (fname) - 1)
						break;
				}
				fname[fnidx] = 0;
			}

			if (*fname)
			{
				char out_file[PATH_MAX];
				snprintf (out_file, sizeof (out_file), "%s/%s%s", dest, new_path, fname);

				char *slash = strrchr (out_file, '/');
				if (slash)
				{
					*slash = 0;
					CreatePath (out_file, true);
					*slash = '/';
				}

				const u64 abs_data = (u64)data_start + f_data_off;
				if (abs_data + f_data_sz <= raw_size)
				{
					if (!testmode && f_data_sz > 0)
						SaveFile (out_file, 0, 0, raw + abs_data, (uint)f_data_sz, 0);
				}
			}

			cur_file_off = next_file_sib;
		}
	}

	// Read children directories
	if (first_child_off != 0xFFFFFFFF)
		RomFS_ReadDirectories (
			raw, raw_size, dir_start, file_start, data_start, first_child_off, new_path, dest);

	// Read next sibling directories
	if (next_sibling_off != 0xFFFFFFFF)
		RomFS_ReadDirectories (
			raw, raw_size, dir_start, file_start, data_start, next_sibling_off, current_path, dest);
}

enumError ExtractROMFSArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".romfs") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 0x5c || memcmp (raw, "IVFC", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// RomFS Header (IVFC)
	// 0x00: "IVFC"
	// 0x04: u32 magic number (0x10000)
	// 0x08: u32 master hash size
	// Level 1:
	// 0x0C: u64 level 1 logical offset
	// 0x14: u64 level 1 hash data offset
	// 0x1C: u32 level 1 block size log2
	// 0x20: u32 reserved
	// Level 2:
	// 0x24: u64 level 2 logical offset
	// 0x2C: u64 level 2 hash data offset
	// 0x34: u32 level 2 block size log2
	// 0x38: u32 reserved
	// Level 3:
	// 0x3C: u64 level 3 logical offset
	// 0x44: u64 level 3 hash data offset
	// 0x4C: u32 level 3 block size log2
	// 0x50: u32 reserved
	// 0x54: u32 reserved
	// 0x58: u32 optional info size
	const u32 master_hash_sz = rd_le32 (raw + 8);
	const u32 l1_block_log2 = rd_le32 (raw + 0x1C);

	uint l3_pos = 0x5c + master_hash_sz;
	l3_pos = (l3_pos + 15) & ~15;
	const uint align_mask = (1u << (l1_block_log2 <= 16 ? l1_block_log2 : 9)) - 1;
	l3_pos = (l3_pos + align_mask) & ~align_mask;

	if (l3_pos + 0x28 > raw_size)
	{
		// Try alternate: direct Level 3 offset from header
		const u64 l3_hdr_off = rd_le64 (raw + 0x3C);
		if (l3_hdr_off > 0 && l3_hdr_off + 0x28 <= raw_size)
			l3_pos = (uint)l3_hdr_off;
		else
		{
			FREE (raw);
			return ERR_INVALID_DATA;
		}
	}

	// Level 3 Header (0x28 bytes):
	// 0x00: u32 HeaderLength
	// 0x04: u32 DirectoryHashTableOffset
	// 0x08: u32 DirectoryHashTableSize
	// 0x0C: u32 DirectoryMetaDataTableOffset
	// 0x10: u32 DirectoryMetaDataTableSize
	// 0x14: u32 FileHashTableOffset
	// 0x18: u32 FileHashTableSize
	// 0x1C: u32 FileMetaDataTableOffset
	// 0x20: u32 FileMetaDataTableSize
	// 0x24: u32 FileDataOffset
	const u32 dir_meta_off = rd_le32 (raw + l3_pos + 0x0C);
	const u32 file_meta_off = rd_le32 (raw + l3_pos + 0x1C);
	const u32 file_data_off = rd_le32 (raw + l3_pos + 0x24);

	const uint dir_start = l3_pos + dir_meta_off;
	const uint file_start = l3_pos + file_meta_off;
	const uint data_start = l3_pos + file_data_off;

	if (dir_start >= raw_size || file_start >= raw_size || data_start > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ROMFS:%s -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	RomFS_ReadDirectories (raw, raw_size, dir_start, file_start, data_start, 0, "", dest);

	FREE (raw);
	return ERR_OK;
}
