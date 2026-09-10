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
// 8. Grezzo Zelda / Luigi's Mansion 3DS Archive (.zar / .gar)
// ----------------------------------------------------------------------------
enumError ExtractGARArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".zar") && !is_ext_match (arg, ".gar") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 0x20)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const bool is_zar1 = !memcmp (raw, "ZAR\x01", 4);
	const bool is_gar = !memcmp (raw, "GAR", 3) && raw[3] >= 2 && raw[3] <= 5;
	if (!is_zar1 && !is_gar)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 declared_file_size = rd_le32 (raw + 4);
	const u16 file_group_count = rd_le16 (raw + 8);
	const u16 file_count = rd_le16 (raw + 10);
	const u32 file_group_offset = rd_le32 (raw + 12);
	const u32 file_info_offset = rd_le32 (raw + 16);
	const u32 data_offset = rd_le32 (raw + 20);
	const char *codename = (const char *)(raw + 24);
	(void)declared_file_size;

	if (file_group_offset >= raw_size || file_info_offset >= raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	const bool is_zelda
		= !memcmp (codename, "queen\0\0\0", 8) || !memcmp (codename, "jenkins\0", 8);
	(void)is_zelda;
	const bool is_system
		= !memcmp (codename, "agora\0\0\0", 8) || !memcmp (codename, "SYSTEM\0\0", 8);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT GAR/ZAR:%s (%u files, %u groups, %.*s) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, file_count, file_group_count, 8,
			codename, dest);

	if (is_system)
	{
		// System Grezzo Archive (Luigi's Mansion 3DS / agora / SYSTEM)
		uint info_pos = file_info_offset;
		for (uint g = 0; g < file_group_count; g++)
		{
			const uint grp_off = file_group_offset + g * 0x20;
			if (grp_off + 20 > raw_size)
				break;

			const u32 grp_file_count = rd_le32 (raw + grp_off);
			const u32 ext_str_off = rd_le32 (raw + grp_off + 12);

			char ext[32] = "";
			if (ext_str_off < raw_size)
			{
				const char *s = (const char *)(raw + ext_str_off);
				size_t slen = strnlen (s, sizeof (ext) - 1);
				memcpy (ext, s, slen);
				ext[slen] = 0;
			}

			for (uint f = 0; f < grp_file_count; f++)
			{
				if (info_pos + 16 > raw_size)
					break;

				const u32 f_size = rd_le32 (raw + info_pos);
				const u32 f_offset = rd_le32 (raw + info_pos + 4);
				const u32 name_str_off = rd_le32 (raw + info_pos + 8);
				info_pos += 16;

				char name[PATH_MAX];
				if (name_str_off < raw_size)
				{
					const char *s = (const char *)(raw + name_str_off);
					size_t slen = strnlen (s, sizeof (name) - 34);
					memcpy (name, s, slen);
					name[slen] = 0;
				}
				else
				{
					snprintf (name, sizeof (name), "file_%u_%u", g, f);
				}

				char out_path[PATH_MAX];
				if (ext[0])
					snprintf (out_path, sizeof (out_path), "%s/%s.%s", dest, name, ext);
				else
					snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

				char *slash = strrchr (out_path, '/');
				if (slash)
				{
					*slash = 0;
					CreatePath (out_path, true);
					*slash = '/';
				}

				if (!testmode && f_size > 0 && f_offset < raw_size)
				{
					u32 actual_sz = f_size;
					if (f_offset + actual_sz > raw_size)
						actual_sz = (u32)(raw_size - f_offset);
					SaveFile (out_path, 0, 0, raw + f_offset, actual_sz, 0);
				}
			}
		}
	}
	else
	{
		// Zelda Grezzo Archive (OoT3D / MM3D - queen / jenkins)
		// Data offsets array is at data_offset
		if (data_offset + (uint64_t)file_count * 4 > raw_size)
		{
			FREE (raw);
			return ERR_INVALID_DATA;
		}

		uint info_pos = file_info_offset;
		uint file_idx = 0;

		for (uint g = 0; g < file_group_count; g++)
		{
			const uint grp_off = file_group_offset + g * 16;
			if (grp_off + 16 > raw_size)
				break;
			const u32 grp_file_count = rd_le32 (raw + grp_off);

			for (uint f = 0; f < grp_file_count && file_idx < file_count; f++, file_idx++)
			{
				const u32 data_payload_off = rd_le32 (raw + data_offset + file_idx * 4);
				u32 f_size = 0;
				u32 name_str_off = 0;

				if (is_zar1)
				{
					// ZarFileInfo: FileSize (4), FileName offset (4)
					if (info_pos + 8 > raw_size)
						break;
					f_size = rd_le32 (raw + info_pos);
					name_str_off = rd_le32 (raw + info_pos + 4);
					info_pos += 8;
				}
				else
				{
					// GarFileInfo: FileSize (4), Name offset (4), FileName offset (4)
					if (info_pos + 12 > raw_size)
						break;
					f_size = rd_le32 (raw + info_pos);
					// Name offset at +4, FileName offset at +8
					name_str_off = rd_le32 (raw + info_pos + 8);
					if (!name_str_off || name_str_off >= raw_size)
						name_str_off = rd_le32 (raw + info_pos + 4);
					info_pos += 12;
				}

				char name[PATH_MAX];
				if (name_str_off < raw_size)
				{
					const char *s = (const char *)(raw + name_str_off);
					size_t slen = strnlen (s, sizeof (name) - 1);
					memcpy (name, s, slen);
					name[slen] = 0;
				}
				else
				{
					snprintf (name, sizeof (name), "file_%04u.bin", file_idx);
				}

				char out_path[PATH_MAX];
				snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

				char *slash = strrchr (out_path, '/');
				if (slash)
				{
					*slash = 0;
					CreatePath (out_path, true);
					*slash = '/';
				}

				if (!testmode && f_size > 0 && data_payload_off < raw_size)
				{
					u32 actual_sz = f_size;
					if (data_payload_off + actual_sz > raw_size)
						actual_sz = (u32)(raw_size - data_payload_off);
					SaveFile (out_path, 0, 0, raw + data_payload_off, actual_sz, 0);
				}
			}
		}
	}

	FREE (raw);
	return ERR_OK;
}


// Grezzo Zelda / Luigi's Mansion 3DS Archive (.zar / ZAR\x01), little-endian
enumError CreateGARArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	// Layout:
	// 0x00..0x1F: Header (32 bytes)
	// 0x20: Filenames string table (aligned to 16)
	// Group table: 1 group (0x10 bytes)
	// Info table: n_entries * 8 bytes (FileSize:4, FileNameOffset:4)
	// Data offset table: n_entries * 4 bytes
	// Payloads: aligned to 16 bytes

	u32 str_tbl_len = 0;
	for (uint i = 0; i < n_entries; i++)
		str_tbl_len += (u32)strlen (leaf_name (sorted[i].name)) + 1;

	const u32 str_tbl_off = 0x20;
	const u32 grp_off = align_up (str_tbl_off + str_tbl_len, 16);
	const u32 info_off = grp_off + 16;
	const u32 data_tbl_off = info_off + n_entries * 8;
	const u32 first_data_off = align_up (data_tbl_off + n_entries * 4, 16);

	u32 total = first_data_off;
	for (uint i = 0; i < n_entries; i++)
		total = align_up (total + sorted[i].size, 16);

	u8 *buf = CALLOC (total, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (buf, "ZAR\x01", 4);
	wr_le32 (buf + 4, total);
	wr_le16 (buf + 8, 1); // file_group_count
	wr_le16 (buf + 10, (u16)n_entries); // file_count
	wr_le32 (buf + 12, grp_off); // file_group_offset
	wr_le32 (buf + 16, info_off); // file_info_offset
	wr_le32 (buf + 20, data_tbl_off); // data_offset
	memcpy (buf + 24, "queen\0\0\0", 8); // codename

	// Group record
	wr_le32 (buf + grp_off, n_entries);

	u32 cur_str_off = str_tbl_off;
	u32 cur_data_off = first_data_off;

	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = leaf_name (sorted[i].name);
		const size_t nlen = strlen (name);
		memcpy (buf + cur_str_off, name, nlen + 1);

		wr_le32 (buf + info_off + i * 8, sorted[i].size);
		wr_le32 (buf + info_off + i * 8 + 4, cur_str_off);

		wr_le32 (buf + data_tbl_off + i * 4, cur_data_off);

		if (sorted[i].data && sorted[i].size)
			memcpy (buf + cur_data_off, sorted[i].data, sorted[i].size);

		cur_str_off += (u32)nlen + 1;
		cur_data_off = align_up (cur_data_off + sorted[i].size, 16);
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = total;
	return ERR_OK;
}
