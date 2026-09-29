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

	if (raw_size < 4 || (memcmp (raw, "XPCK", 4) && memcmp (raw, "XPC2", 4)))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size < 0x10)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	const u32 file_count = (uint)(rd_le16 (raw + 4) & 0xFFF);
	const u32 file_info_offset = (u32)rd_le16 (raw + 6) * 4;
	const u32 file_table_offset = (u32)rd_le16 (raw + 8) * 4;
	const u32 data_offset = (u32)rd_le16 (raw + 10) * 4;
	const u32 filename_table_size = (u32)rd_le16 (raw + 14) * 4;

	if (!file_count || file_info_offset < 0x10
		|| (u64)file_info_offset + (u64)file_count * 12 > raw_size
		|| (u64)file_table_offset + filename_table_size > raw_size || data_offset > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}
	// Validate every member before writing anything. A partial payload must
	// never be reported as a successfully extracted file.
	for (uint i = 0; i < file_count; i++)
	{
		const u8 *entry = raw + file_info_offset + i * 12;
		const u32 rel_off = rd_le16 (entry + 6) | (u32)entry[10] << 16;
		const u32 size = rd_le16 (entry + 8) | (u32)entry[11] << 16;
		if ((u64)data_offset + (u64)rel_off * 4 + size > raw_size)
		{
			FREE (raw);
			return ERR_INVALID_DATA;
		}
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	if (!testmode && (err = CreatePath (dest, true)))
	{
		FREE (raw);
		return err;
	}

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT XPCK:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, file_count, dest);

	// Try reading filename table if present
	const char *names_ptr = (const char *)(raw + file_table_offset);
	uint name_pos = 0;

	for (uint i = 0; i < file_count; i++)
	{
		const u32 entry_off = file_info_offset + i * 12;
		u32 off = (u32)rd_le16 (raw + entry_off + 6);
		u32 sz = (u32)rd_le16 (raw + entry_off + 8);
		const u32 off_ext = (u32)raw[entry_off + 10];
		const u32 sz_ext = (u32)raw[entry_off + 11];

		off |= (off_ext << 16);
		sz |= (sz_ext << 16);
		const u64 off64 = (u64)off * 4 + data_offset;
		char fname[240];
		if (name_pos < filename_table_size)
		{
			const size_t max_len = filename_table_size - name_pos;
			const size_t slen = strnlen (names_ptr + name_pos, max_len);
			if (slen < max_len && slen < sizeof (fname))
			{
				memcpy (fname, names_ptr + name_pos, slen);
				fname[slen] = 0;
			}
			else
				fname[0] = 0;
			// Consume the complete stored name even when it cannot be used.
			name_pos += (uint)slen + (slen < max_len);
			if (!OwnedNameOk (fname))
				snprintf (fname, sizeof (fname), "file_%04u.bin", i);
		}
		else
		{
			snprintf (fname, sizeof (fname), "file_%04u.bin", i);
		}

		char out_path[PATH_MAX];
		if (snprintf (out_path, sizeof (out_path), "%s/%s", dest, fname) >= sizeof (out_path))
		{
			err = ERR_INVALID_DATA;
			break;
		}

		if (!testmode && (err = SaveFile (out_path, 0, 0, raw + off64, sz, 0)))
			break;
	}

	FREE (raw);
	return err;
}

static int compare_xpck_entries (const void *a, const void *b)
{
	const nintendo_sarc_entry_t *ea = a, *eb = b;
	return strcmp (leaf_name (ea->name), leaf_name (eb->name));
}

// 1. Level-5 Container Archive (.xc / .xpck)
enumError CreateXPCKArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size)
		return ERR_INVALID_DATA;
	*dest = 0;
	*dest_size = 0;
	if (!entries || !n_entries || n_entries > 0xFFF)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_xpck_entries);

	const u32 header_sz = 0x10;
	const u32 file_info_sz = n_entries * 12;
	const u32 file_names_start = header_sz + file_info_sz;

	u64 names_len = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = sorted[i].name ? sorted[i].name : "";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;
		if (!OwnedNameOk (name) || (sorted[i].size && !sorted[i].data)
			|| (i && !strcmp (name, leaf_name (sorted[i - 1].name))))
		{
			FREE (sorted);
			return ERR_INVALID_DATA;
		}
		names_len += strlen (name) + 1;
	}
	// Check collisions using stored names while retaining source-path order.
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);
	const u64 filename_table_size = (names_len + 3) & ~3ull;
	const u64 data_start = (file_names_start + filename_table_size + 15) & ~15ull;

	if (file_names_start / 4 > 0xFFFF || data_start / 4 > 0xFFFF
		|| filename_table_size / 4 > 0xFFFF)
	{
		FREE (sorted);
		return EFBIG;
	}

	u64 cur_data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		if (sorted[i].size > 0xFFFFFF)
		{
			FREE (sorted);
			return EFBIG;
		}
		cur_data_off = (cur_data_off + sorted[i].size + 3) & ~3ull;
		if (cur_data_off > 0xFFFFFFFFull || (cur_data_off - data_start) / 4 > 0xFFFFFF)
		{
			FREE (sorted);
			return EFBIG;
		}
	}

	u8 *buf = CALLOC ((size_t)cur_data_off, 1);
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

enumError create_xpck_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateXPCKArchive (&data, &size, list.entry, list.used);
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
