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
// 1. Level-5 Container Archive (.xc / .xpck / XPCK / XPC2)
// ----------------------------------------------------------------------------
enumError ExtractXPCKArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".xc") && !is_ext_match (arg, ".xpck") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 0x20 || (memcmp (raw, "XPCK", 4) && memcmp (raw, "XPC2", 4)))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 file_count = (uint)(rd_le16 (raw + 4) & 0xFFF);
	const u32 file_info_offset = (u32)rd_le16 (raw + 6) * 4;
	const u32 file_table_offset = (u32)rd_le16 (raw + 8) * 4;
	const u32 data_offset = (u32)rd_le16 (raw + 10) * 4;
	const u32 filename_table_size = (u32)rd_le16 (raw + 14) * 4;

	if (file_info_offset >= raw_size || file_table_offset >= raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT XPCK:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, file_count, dest);

	// Try reading filename table if present
	const char *names_ptr = (file_table_offset + filename_table_size <= raw_size)
		? (const char *)(raw + file_table_offset)
		: 0;
	uint name_pos = 0;

	for (uint i = 0; i < file_count; i++)
	{
		const u32 entry_off = file_info_offset + i * 12;
		if (entry_off + 12 > raw_size)
			break;

		u32 off = (u32)rd_le16 (raw + entry_off + 6);
		u32 sz = (u32)rd_le16 (raw + entry_off + 8);
		const u32 off_ext = (u32)raw[entry_off + 10];
		const u32 sz_ext = (u32)raw[entry_off + 11];

		off |= (off_ext << 16);
		sz |= (sz_ext << 16);
		off = off * 4 + data_offset;

		if (off + sz > raw_size)
			sz = raw_size > off ? (uint)(raw_size - off) : 0;

		char fname[64];
		if (names_ptr && name_pos < filename_table_size && names_ptr[name_pos])
		{
			snprintf (fname, sizeof (fname), "%s", names_ptr + name_pos);
			name_pos += strlen (names_ptr + name_pos) + 1;
		}
		else
		{
			snprintf (fname, sizeof (fname), "file_%04u.bin", i);
		}

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, fname);

		if (!testmode && sz > 0)
			SaveFile (out_path, 0, 0, raw + off, sz, 0);
	}

	FREE (raw);
	return ERR_OK;
}


// 1. Level-5 Container Archive (.xc / .xpck)
enumError CreateXPCKArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	const u32 header_sz = 0x10;
	const u32 file_info_sz = n_entries * 12;
	const u32 file_names_start = header_sz + file_info_sz;

	u32 names_len = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = sorted[i].name ? sorted[i].name : "";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;
		names_len += strlen (name) + 1;
	}
	const u32 filename_table_size = (names_len + 3) & ~3;
	const u32 data_start = (file_names_start + filename_table_size + 15) & ~15;

	u32 cur_data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
		cur_data_off = (cur_data_off + sorted[i].size + 3) & ~3;

	u8 *buf = CALLOC (cur_data_off, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (buf, "XPCK", 4);
	wr_le16 (buf + 4, (u16)(n_entries & 0xFFF));
	wr_le16 (buf + 6, (u16)(header_sz / 4));
	wr_le16 (buf + 8, (u16)(file_names_start / 4));
	wr_le16 (buf + 10, (u16)(data_start / 4));
	wr_le16 (buf + 12, 0);
	wr_le16 (buf + 14, (u16)(filename_table_size / 4));

	u32 name_write_pos = file_names_start;
	u32 data_off = data_start;

	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = sorted[i].name ? sorted[i].name : "";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;

		const size_t nlen = strlen (name);
		memcpy (buf + name_write_pos, name, nlen + 1);
		name_write_pos += nlen + 1;

		const u32 eoff = header_sz + i * 12;
		u32 crc = (u32)crc32 (0, (const Bytef *)name, nlen);
		wr_le32 (buf + eoff, crc);

		u32 rel_off = (data_off - data_start) / 4;
		u32 sz = sorted[i].size;

		wr_le16 (buf + eoff + 6, (u16)(rel_off & 0xFFFF));
		wr_le16 (buf + eoff + 8, (u16)(sz & 0xFFFF));
		buf[eoff + 10] = (u8)((rel_off >> 16) & 0xFF);
		buf[eoff + 11] = (u8)((sz >> 16) & 0xFF);

		if (sorted[i].data && sorted[i].size > 0)
			memcpy (buf + data_off, sorted[i].data, sorted[i].size);

		data_off = (data_off + sorted[i].size + 3) & ~3;
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = cur_data_off;
	return ERR_OK;
}
