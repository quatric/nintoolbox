// SPDX-License-Identifier: GPL-2.0+
// Split out of lib-nintendo-archives.c -- one archive format per file.
#include "lib-nintendo-archives.h"
#include "lib-nintendo.h"
#include "lib-romfs.h"
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

bool IsROMFS (const u8 *data, uint size)
{
	if (!data || size < 0x50)
		return false;

	if (size >= 0x5c && !memcmp (data, "IVFC", 4))
		return true;

	if (rd_le64 (data) == 0x50)
	{
		const u64 dir_meta_off = rd_le64 (data + 0x18);
		const u64 dir_meta_sz = rd_le64 (data + 0x20);
		const u64 file_meta_off = rd_le64 (data + 0x38);
		const u64 file_meta_sz = rd_le64 (data + 0x40);
		const u64 file_data_off = rd_le64 (data + 0x48);

		if (dir_meta_off >= 0x50 && file_meta_off >= 0x50 && file_data_off >= 0x50
			&& file_data_off >= file_meta_off + file_meta_sz && dir_meta_sz > 0)
			return true;
	}

	return false;
}

// Extract Nintendo 3DS RomFS Archive (.romfs / IVFC with UTF-16LE strings)
static void RomFS_ReadDirectories_3DS (const u8 *raw, size_t raw_size, uint dir_start,
	uint file_start, uint data_start, uint cur_dir_off, ccp current_path, ccp dest)
{
	if ((u64)dir_start + cur_dir_off + 24 > raw_size)
		return;

	const u8 *dir = raw + dir_start + cur_dir_off;
	const u32 next_sibling_off = rd_le32 (dir + 4);
	const u32 first_child_off = rd_le32 (dir + 8);
	const u32 first_file_off = rd_le32 (dir + 12);
	const u32 name_len = rd_le32 (dir + 20);

	char dir_name[PATH_MAX] = "";
	if (name_len > 0 && (u64)dir_start + cur_dir_off + 24 + name_len <= raw_size)
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
	if (first_file_off != 0xFFFFFFFF && (u64)file_start + first_file_off < raw_size)
	{
		u32 cur_file_off = first_file_off;
		while (cur_file_off != 0xFFFFFFFF && (u64)file_start + cur_file_off + 32 <= raw_size)
		{
			const u8 *file = raw + file_start + cur_file_off;
			const u32 next_file_sib = rd_le32 (file + 4);
			const u64 f_data_off = rd_le64 (file + 8);
			const u64 f_data_sz = rd_le64 (file + 16);
			const u32 f_name_len = rd_le32 (file + 28);

			char fname[PATH_MAX] = "";
			if (f_name_len > 0 && (u64)file_start + cur_file_off + 32 + f_name_len <= raw_size)
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

			if (*fname && OwnedNameOk (fname))
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
		RomFS_ReadDirectories_3DS (
			raw, raw_size, dir_start, file_start, data_start, first_child_off, new_path, dest);

	// Read next sibling directories
	if (next_sibling_off != 0xFFFFFFFF)
		RomFS_ReadDirectories_3DS (
			raw, raw_size, dir_start, file_start, data_start, next_sibling_off, current_path, dest);
}

// Extract Nintendo Switch RomFS Archive (.romfs / 80-byte header with UTF-8 strings)
static void RomFS_ReadDirectories_Switch (const u8 *raw, size_t raw_size, u64 dir_start,
	u64 file_start, u64 data_start, u32 cur_dir_off, ccp current_path, ccp dest)
{
	if (cur_dir_off == 0xFFFFFFFF || dir_start + cur_dir_off + 24 > raw_size)
		return;

	const u8 *dir = raw + dir_start + cur_dir_off;
	const u32 next_sibling_off = rd_le32 (dir + 4);
	const u32 first_child_off = rd_le32 (dir + 8);
	const u32 first_file_off = rd_le32 (dir + 12);
	const u32 name_len = rd_le32 (dir + 20);

	char dir_name[PATH_MAX] = "";
	if (name_len > 0 && dir_start + cur_dir_off + 24 + name_len <= raw_size)
	{
		uint nlen = name_len < sizeof (dir_name) - 1 ? name_len : (uint)sizeof (dir_name) - 1;
		memcpy (dir_name, dir + 24, nlen);
		dir_name[nlen] = 0;
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
				uint fnlen = f_name_len < sizeof (fname) - 1 ? f_name_len : (uint)sizeof (fname) - 1;
				memcpy (fname, file + 32, fnlen);
				fname[fnlen] = 0;
			}

			if (*fname && OwnedNameOk (fname))
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

				const u64 abs_data = data_start + f_data_off;
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
		RomFS_ReadDirectories_Switch (
			raw, raw_size, dir_start, file_start, data_start, first_child_off, new_path, dest);

	// Read next sibling directories
	if (next_sibling_off != 0xFFFFFFFF)
		RomFS_ReadDirectories_Switch (
			raw, raw_size, dir_start, file_start, data_start, next_sibling_off, current_path, dest);
}

enumError ExtractROMFSArchive (ccp arg, ccp basedir, uint depth)
{
	(void)depth;

	const bool is_romfs_ext = is_ext_match (arg, ".romfs");
	const bool is_bin_ext = is_ext_match (arg, ".bin");
	if (!is_romfs_ext && !is_bin_ext)
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 0x50)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	bool is_switch = false;
	u64 dir_start = 0, file_start = 0, data_start = 0;

	if (raw_size >= 0x5c && memcmp (raw, "IVFC", 4) == 0)
	{
		// RomFS Header (IVFC)
		const u32 master_hash_sz = rd_le32 (raw + 8);
		const u32 l1_block_log2 = rd_le32 (raw + 0x1C);

		uint l3_pos = 0x5c + master_hash_sz;
		l3_pos = (l3_pos + 15) & ~15;
		const uint align_mask = (1u << (l1_block_log2 <= 16 ? l1_block_log2 : 9)) - 1;
		l3_pos = (l3_pos + align_mask) & ~align_mask;

		if (l3_pos + 0x28 > raw_size)
		{
			const u64 l3_hdr_off = rd_le64 (raw + 0x3C);
			if (l3_hdr_off > 0 && l3_hdr_off < raw_size && raw_size - l3_hdr_off >= 0x28)
				l3_pos = (uint)l3_hdr_off;
			else
			{
				FREE (raw);
				return ERR_INVALID_DATA;
			}
		}

		// Check if Level 3 header is Switch RomFS (0x50 bytes with 64-bit size == 0x50)
		if (raw_size - l3_pos >= 0x50 && rd_le64 (raw + l3_pos) == 0x50)
		{
			is_switch = true;
			dir_start = (u64)l3_pos + rd_le64 (raw + l3_pos + 0x18);
			file_start = (u64)l3_pos + rd_le64 (raw + l3_pos + 0x38);
			data_start = (u64)l3_pos + rd_le64 (raw + l3_pos + 0x48);
		}
		else
		{
			// 3DS RomFS Level 3 Header (0x28 bytes with 32-bit offsets)
			const u32 dir_meta_off = rd_le32 (raw + l3_pos + 0x0C);
			const u32 file_meta_off = rd_le32 (raw + l3_pos + 0x1C);
			const u32 file_data_off = rd_le32 (raw + l3_pos + 0x24);

			dir_start = (u64)l3_pos + dir_meta_off;
			file_start = (u64)l3_pos + file_meta_off;
			data_start = (u64)l3_pos + file_data_off;
		}
	}
	else if (rd_le64 (raw) == 0x50)
	{
		// Standalone Nintendo Switch RomFS
		is_switch = true;
		dir_start = rd_le64 (raw + 0x18);
		file_start = rd_le64 (raw + 0x38);
		data_start = rd_le64 (raw + 0x48);
	}
	else
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	if (dir_start >= raw_size || file_start >= raw_size || data_start > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT %s ROMFS:%s -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", is_switch ? "Switch" : "3DS", arg, dest);

	if (is_switch)
		RomFS_ReadDirectories_Switch (
			raw, raw_size, dir_start, file_start, data_start, 0, "", dest);
	else
		RomFS_ReadDirectories_3DS (
			raw, raw_size, (uint)dir_start, (uint)file_start, (uint)data_start, 0, "", dest);

	FREE (raw);
	return ERR_OK;
}
