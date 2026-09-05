// SPDX-License-Identifier: GPL-2.0+
#include "lib-nintendo-archives.h"
#include "lib-nintendo.h"
#include "lib-szs.h"
#include "lib-std.h"
#include "lib-zstd.h"
#include <string.h>

static bool is_ext_match (ccp path, ccp ext)
{
	if (!path || !ext)
		return false;
	const size_t plen = strlen (path);
	const size_t elen = strlen (ext);
	if (plen < elen)
		return false;
	return !strcasecmp (path + plen - elen, ext);
}

static void get_dest_dir (char *dest, size_t dest_size, ccp arg, ccp basedir)
{
	if (opt_dest && *opt_dest)
		snprintf (dest, dest_size, "%s", opt_dest);
	else if (basedir && *basedir)
		snprintf (dest, dest_size, "%s", basedir);
	else
	{
		snprintf (dest, dest_size, "%s.d", arg);
	}
}

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
		fprintf (stdlog, "%s%sEXTRACT XPCK:%s (%u files) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, file_count, dest);

	// Try reading filename table if present
	const char *names_ptr = (file_table_offset + filename_table_size <= raw_size)
		? (const char *)(raw + file_table_offset) : 0;
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

// ----------------------------------------------------------------------------
// 2. Camelot Archive Table (.ztab / ZTAB)
// ----------------------------------------------------------------------------
enumError ExtractZTABArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".ztab") && !is_ext_match (arg, ".tab") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 8 || memcmp (raw, "ZTAB", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 count = rd_be32 (raw + 4);
	if (!count || count > 100000 || (uint64_t)8 + (uint64_t)count * 16 > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ZTAB:%s (%u entries) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, count, dest);

	for (uint i = 0; i < count; i++)
	{
		const u32 eoff = 8 + i * 16;
		const u32 flags = rd_be32 (raw + eoff);
		const u32 off = rd_be32 (raw + eoff + 4);
		u32 sz = rd_be32 (raw + eoff + 8);

		if (off + sz > raw_size)
			sz = raw_size > off ? (uint)(raw_size - off) : 0;

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/entry_%04u_flags_%08x.bin", dest, i, flags);

		if (!testmode && sz > 0 && off < raw_size)
			SaveFile (out_path, 0, 0, raw + off, sz, 0);
	}

	FREE (raw);
	return ERR_OK;
}

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
	if ((uint64_t)first_off < (uint64_t)4 + (uint64_t)count * 4 || (uint64_t)first_off + 16 > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT MDR:%s (%u chunks) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, count, dest);

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
			if (comp_sz > 0 && DecodeZlibGrow (&decomp_data, &decomp_sz, raw + off + 16, comp_sz) == ERR_OK && decomp_data)
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

// ----------------------------------------------------------------------------
// 4. Pikmin 1 & 2 Model/Archive Container (.pvol)
// ----------------------------------------------------------------------------
enumError ExtractPVOLArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".pvol"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 12)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 fcount = rd_le32 (raw);
	if (fcount < 2 || fcount > 100000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const uint64_t table_sz = (uint64_t)4 + (uint64_t)(fcount - 1) * 8;
	if (table_sz > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 first_off = rd_le32 (raw + 4);
	if ((uint64_t)first_off < table_sz || (uint64_t)first_off >= raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u32 prev_off = first_off;
	for (uint i = 0; i < fcount - 1; i++)
	{
		const u32 toff = 4 + i * 8;
		const u32 off = rd_le32 (raw + toff);
		const u32 len = rd_le32 (raw + toff + 4);
		if ((uint64_t)off < table_sz || (uint64_t)off + len > raw_size || off < prev_off)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		prev_off = off;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PVOL:%s (%u files) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, fcount - 1, dest);

	for (uint i = 0; i < fcount - 1; i++)
	{
		const u32 toff = 4 + i * 8;
		const u32 off = rd_le32 (raw + toff);
		u32 len = rd_le32 (raw + toff + 4);

		if (off >= raw_size)
			continue;

		char name1[33] = "";
		char name2[33] = "";
		if (off + 0x20 <= raw_size)
			StringCopyS (name1, sizeof (name1), (ccp)(raw + off));
		if (off + 0x28 <= raw_size)
			StringCopyS (name2, sizeof (name2), (ccp)(raw + off + 0x20));

		char full_name[80];
		if (name1[0] || name2[0])
			snprintf (full_name, sizeof (full_name), "%s%s", name1, name2);
		else
			snprintf (full_name, sizeof (full_name), "file_%04u.bin", i);

		const u32 data_off = off + 0x28;
		if (data_off + len > raw_size)
			len = raw_size > data_off ? (uint)(raw_size - data_off) : 0;

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, full_name);

		if (!testmode && len > 0 && data_off < raw_size)
			SaveFile (out_path, 0, 0, raw + data_off, len, 0);
	}

	FREE (raw);
	return ERR_OK;
}

// ----------------------------------------------------------------------------
// 5. Jump Super Stars / Jump Ultimate Stars DS Archive (.srd / .stpk / STPK)
// ----------------------------------------------------------------------------
enumError ExtractSTPKArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".srd") && !is_ext_match (arg, ".stpk") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 0x10 || memcmp (raw, "STPK", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 resource_count = rd_be32 (raw + 8);
	if (!resource_count || resource_count > 100000 || (uint64_t)0x10 + (uint64_t)resource_count * 0x30 > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT STPK:%s (%u resources) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, resource_count, dest);

	for (uint i = 0; i < resource_count; i++)
	{
		const u32 eoff = 0x10 + i * 0x30;
		const u32 off = rd_be32 (raw + eoff);
		u32 sz = rd_be32 (raw + eoff + 4);

		char name[33] = "";
		StringCopyS (name, sizeof (name), (ccp)(raw + eoff + 0x10));
		if (!name[0])
			snprintf (name, sizeof (name), "res_%04u.bin", i);

		if (off + sz > raw_size)
			sz = raw_size > off ? (uint)(raw_size - off) : 0;

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

		if (!testmode && sz > 0 && off < raw_size)
			SaveFile (out_path, 0, 0, raw + off, sz, 0);
	}

	FREE (raw);
	return ERR_OK;
}

// ----------------------------------------------------------------------------
// 6. GameCube Resource Archive (.res / res\n)
// ----------------------------------------------------------------------------
enumError ExtractF9ResArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".res") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 0x24 || memcmp (raw, "res\n", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 header_offset = rd_be32 (raw + 8);
	const u32 chunks_offset = rd_be32 (raw + 0x1C);

	if (chunks_offset + 8 > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	const u32 chunk_count = rd_be32 (raw + chunks_offset);
	if (!chunk_count || chunk_count > 100000 || (uint64_t)chunks_offset + 8 + (uint64_t)chunk_count * 20 > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT RES:%s (%u chunks) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, chunk_count, dest);

	for (uint i = 0; i < chunk_count; i++)
	{
		const u32 coff = chunks_offset + 8 + i * 20;
		char tag[5] = { 0 };
		memcpy (tag, raw + coff, 4);
		for (int c = 0; c < 4; c++)
			if (tag[c] < 32 || tag[c] > 126) tag[c] = '_';

		const u32 off = rd_be32 (raw + coff + 4) + header_offset;
		u32 sz = rd_be32 (raw + coff + 8);

		if (off + sz > raw_size)
			sz = raw_size > off ? (uint)(raw_size - off) : 0;

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s_%04u.bin", dest, tag, i);

		if (!testmode && sz > 0 && off < raw_size)
			SaveFile (out_path, 0, 0, raw + off, sz, 0);
	}

	FREE (raw);
	return ERR_OK;
}

// ----------------------------------------------------------------------------
// Repacking / Creation Implementations
// ----------------------------------------------------------------------------

#include <zlib.h>
#include <stdlib.h>

static int compare_archive_entries (const void *a, const void *b)
{
	const nintendo_sarc_entry_t *ea = (const nintendo_sarc_entry_t *)a;
	const nintendo_sarc_entry_t *eb = (const nintendo_sarc_entry_t *)b;
	ccp na = ea->name ? ea->name : "";
	ccp nb = eb->name ? eb->name : "";
	return strcmp (na, nb);
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

// 2. Camelot Archive Table (.ztab / .tab)
enumError CreateZTABArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	const u32 header_sz = 8;
	const u32 table_sz = n_entries * 16;
	u32 data_start = (header_sz + table_sz + 15) & ~15;

	u32 cur_data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
		cur_data_off = (cur_data_off + sorted[i].size + 15) & ~15;

	u8 *buf = CALLOC (cur_data_off, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (buf, "ZTAB", 4);
	wr_be32 (buf + 4, n_entries);

	u32 data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		const u32 eoff = header_sz + i * 16;
		u32 flags = 0;
		ccp name = sorted[i].name ? sorted[i].name : "";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;

		const char *fpos = strstr (name, "flags_");
		if (fpos)
			sscanf (fpos + 6, "%x", &flags);

		wr_be32 (buf + eoff, flags);
		wr_be32 (buf + eoff + 4, data_off);
		wr_be32 (buf + eoff + 8, sorted[i].size);
		wr_be32 (buf + eoff + 12, 0);

		if (sorted[i].data && sorted[i].size > 0)
			memcpy (buf + data_off, sorted[i].data, sorted[i].size);

		data_off = (data_off + sorted[i].size + 15) & ~15;
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = cur_data_off;
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

// 4. Pikmin 1 & 2 Model/Archive Container (.pvol)
enumError CreatePVOLArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	const u32 fcount = n_entries + 1;
	const u32 table_sz = 4 + (fcount - 1) * 8;
	u32 data_start = (table_sz + 31) & ~31;

	u32 cur_off = data_start;
	for (uint i = 0; i < n_entries; i++)
		cur_off = (cur_off + 0x28 + sorted[i].size + 15) & ~15;

	u8 *buf = CALLOC (cur_off, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	wr_le32 (buf, fcount);

	u32 off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		const u32 toff = 4 + i * 8;
		wr_le32 (buf + toff, off);
		wr_le32 (buf + toff + 4, sorted[i].size);

		ccp name = sorted[i].name ? sorted[i].name : "";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;

		const size_t nlen = strlen (name);
		if (nlen <= 32)
			memcpy (buf + off, name, nlen);
		else
		{
			memcpy (buf + off, name, 32);
			const size_t rem = nlen - 32 < 8 ? nlen - 32 : 8;
			memcpy (buf + off + 0x20, name + 32, rem);
		}

		if (sorted[i].data && sorted[i].size > 0)
			memcpy (buf + off + 0x28, sorted[i].data, sorted[i].size);

		off = (off + 0x28 + sorted[i].size + 15) & ~15;
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = cur_off;
	return ERR_OK;
}

// 5. Jump Super Stars / Jump Ultimate Stars DS Archive (.srd / .stpk)
enumError CreateSTPKArchive (
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
	const u32 table_sz = n_entries * 0x30;
	u32 data_start = (header_sz + table_sz + 15) & ~15;

	u32 cur_data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
		cur_data_off = (cur_data_off + sorted[i].size + 15) & ~15;

	u8 *buf = CALLOC (cur_data_off, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (buf, "STPK", 4);
	wr_be32 (buf + 4, 1);
	wr_be32 (buf + 8, n_entries);
	wr_be32 (buf + 12, 0);

	u32 data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		const u32 eoff = header_sz + i * 0x30;
		wr_be32 (buf + eoff, data_off);
		wr_be32 (buf + eoff + 4, sorted[i].size);

		ccp name = sorted[i].name ? sorted[i].name : "";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;
		strncpy ((char *)(buf + eoff + 0x10), name, 31);

		if (sorted[i].data && sorted[i].size > 0)
			memcpy (buf + data_off, sorted[i].data, sorted[i].size);

		data_off = (data_off + sorted[i].size + 15) & ~15;
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = cur_data_off;
	return ERR_OK;
}

// 6. GameCube Resource Archive (.res / res\n)
enumError CreateF9ResArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	const u32 header_offset = 0x20;
	const u32 chunks_offset = 0x20;
	const u32 chunks_table_sz = 8 + n_entries * 20;
	u32 data_start = (chunks_offset + chunks_table_sz + 15) & ~15;

	u32 cur_data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
		cur_data_off = (cur_data_off + sorted[i].size + 15) & ~15;

	u8 *buf = CALLOC (cur_data_off, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (buf, "res\n", 4);
	wr_be32 (buf + 8, header_offset);
	wr_be32 (buf + 0x1C, chunks_offset);

	wr_be32 (buf + chunks_offset, n_entries);
	wr_be32 (buf + chunks_offset + 4, 4);

	u32 data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		const u32 coff = chunks_offset + 8 + i * 20;
		ccp name = sorted[i].name ? sorted[i].name : "";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;

		char tag[5] = "DATA";
		for (int c = 0; c < 4 && name[c] && name[c] != '_'; c++)
			tag[c] = name[c];

		memcpy (buf + coff, tag, 4);
		wr_be32 (buf + coff + 4, data_off - header_offset);
		wr_be32 (buf + coff + 8, sorted[i].size);
		wr_be32 (buf + coff + 12, 0);
		wr_be32 (buf + coff + 16, 0);

		if (sorted[i].data && sorted[i].size > 0)
			memcpy (buf + data_off, sorted[i].data, sorted[i].size);

		data_off = (data_off + sorted[i].size + 15) & ~15;
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = cur_data_off;
	return ERR_OK;
}

// ----------------------------------------------------------------------------
// 7. NES Remix indieszero Archive (.zlarc)
// ----------------------------------------------------------------------------
enumError ExtractZLARCArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".zlarc") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *comp = 0;
	size_t comp_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &comp, &comp_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (comp_size < 16)
	{
		FREE (comp);
		return ERR_NOTHING_TO_DO;
	}

	u8 *raw = 0;
	uint raw_size = 0;
	err = DecodeZlibGrow (&raw, &raw_size, comp, comp_size);
	FREE (comp);

	if (err || !raw || raw_size < 16)
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
	if ((uint64_t)first_off < (uint64_t)4 + (uint64_t)count * 4 || (uint64_t)first_off + 12 > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Calculate data_start: end of all descriptors
	u32 data_start = 0;
	for (uint i = 0; i < count; i++)
	{
		const u32 desc_off = rd_be32 (raw + 4 + i * 4);
		if (desc_off + 12 > raw_size)
			continue;
		const u32 name_len = rd_be32 (raw + desc_off + 8);
		const u32 meta_end = desc_off + 12 + name_len;
		if (meta_end > data_start)
			data_start = meta_end;
	}

	if (data_start >= raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ZLARC:%s (%u files) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, count, dest);

	for (uint i = 0; i < count; i++)
	{
		const u32 desc_off = rd_be32 (raw + 4 + i * 4);
		if (desc_off + 12 > raw_size)
			continue;

		const u32 data_off = rd_be32 (raw + desc_off);
		u32 data_sz = rd_be32 (raw + desc_off + 4);
		const u32 name_len = rd_be32 (raw + desc_off + 8);

		char name[PATH_MAX];
		if (desc_off + 12 + name_len <= raw_size && name_len > 0)
		{
			uint cpy = name_len < sizeof (name) - 1 ? name_len : (uint)sizeof (name) - 1;
			memcpy (name, raw + desc_off + 12, cpy);
			name[cpy] = 0;
			while (cpy > 0 && name[cpy - 1] == 0)
				name[--cpy] = 0;
		}
		else
		{
			snprintf (name, sizeof (name), "file_%04u.bin", i);
		}

		const u32 file_off = data_start + data_off;
		if (file_off + data_sz > raw_size)
			data_sz = raw_size > file_off ? (uint)(raw_size - file_off) : 0;

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

		char *slash = strrchr (out_path, '/');
		if (slash)
		{
			*slash = 0;
			CreatePath (out_path, true);
			*slash = '/';
		}

		if (!testmode && data_sz > 0 && file_off < raw_size)
			SaveFile (out_path, 0, 0, raw + file_off, data_sz, 0);
	}

	FREE (raw);
	return ERR_OK;
}

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

	const bool is_zelda = !memcmp (codename, "queen\0\0\0", 8) || !memcmp (codename, "jenkins\0", 8);
	(void)is_zelda;
	const bool is_system = !memcmp (codename, "agora\0\0\0", 8) || !memcmp (codename, "SYSTEM\0\0", 8);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT GAR/ZAR:%s (%u files, %u groups, %.*s) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, file_count, file_group_count,
			8, codename, dest);

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

// ----------------------------------------------------------------------------
// 9. Mario Kart Arcade GP DX Layout Archive (.pac / pack)
// ----------------------------------------------------------------------------
enumError ExtractMKGPDXPacArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".pac") && !is_ext_match (arg, ".bin"))
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

// ----------------------------------------------------------------------------
// 10. Twilight Princess HD / Zelda TMPK Archive (.pack / TMPK)
// ----------------------------------------------------------------------------
enumError ExtractTMPKArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".pack") && !is_ext_match (arg, ".bin") && !is_ext_match (arg, ".tmpk"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 16 || memcmp (raw, "TMPK", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 file_count = rd_be32 (raw + 4);
	const u32 alignment = rd_be32 (raw + 8);

	if (!file_count || file_count > 100000 || 16 + (uint64_t)file_count * 16 > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT TMPK:%s (%u files, align=%u) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, file_count, alignment, dest);

	for (uint i = 0; i < file_count; i++)
	{
		const uint entry_pos = 16 + i * 16;
		const u32 name_offset = rd_be32 (raw + entry_pos);
		const u32 file_offset = rd_be32 (raw + entry_pos + 4);
		u32 file_size = rd_be32 (raw + entry_pos + 8);

		char name[PATH_MAX];
		if (name_offset < raw_size)
		{
			const char *s = (const char *)(raw + name_offset);
			size_t slen = strnlen (s, sizeof (name) - 1);
			memcpy (name, s, slen);
			name[slen] = 0;
		}
		else
		{
			snprintf (name, sizeof (name), "file_%04u.bin", i);
		}

		if (file_offset + file_size > raw_size)
			file_size = raw_size > file_offset ? (u32)(raw_size - file_offset) : 0;

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

		char *slash = strrchr (out_path, '/');
		if (slash)
		{
			*slash = 0;
			CreatePath (out_path, true);
			*slash = '/';
		}

		if (!testmode && file_size > 0 && file_offset < raw_size)
			SaveFile (out_path, 0, 0, raw + file_offset, file_size, 0);
	}

	FREE (raw);
	return ERR_OK;
}

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
		fprintf (stdlog, "%s%sEXTRACT NXARC:%s (%u files) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, file_count, dest);

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
		const u64 size = (u64)rd_le32(raw + entry_pos) | ((u64)rd_le32(raw + entry_pos + 4) << 32);
		const u64 offset = (u64)rd_le32(raw + entry_pos + 8) | ((u64)rd_le32(raw + entry_pos + 12) << 32);
		const u64 flag = (u64)rd_le32(raw + entry_pos + 16) | ((u64)rd_le32(raw + entry_pos + 20) << 32);

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

// ----------------------------------------------------------------------------
// 12. Nintendo APAK Archive (.apak / APAK)
// ----------------------------------------------------------------------------
enumError ExtractAPAKArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".apak") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 24 || memcmp (raw, "APAK", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Detect endianness: version at offset 6 is 5
	const u16 ver_be = rd_be16 (raw + 6);
	const bool big = (ver_be == 5);

	const u32 file_count = big ? rd_be32 (raw + 8) : rd_le32 (raw + 8);
	const u32 unknown1 = big ? rd_be32 (raw + 12) : rd_le32 (raw + 12);
	const u32 file_info_size = big ? rd_be32 (raw + 16) : rd_le32 (raw + 16);
	(void)unknown1;
	(void)file_info_size;

	if (!file_count || file_count > 100000 || 24 + (uint64_t)file_count * 64 > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT APAK:%s (%u files, %s-endian) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, file_count,
			big ? "big" : "little", dest);

	for (uint i = 0; i < file_count; i++)
	{
		const uint entry_pos = 24 + i * 64;
		const u32 data_offset = big ? rd_be32 (raw + entry_pos + 4) : rd_le32 (raw + entry_pos + 4);
		u32 file_size = big ? rd_be32 (raw + entry_pos + 8) : rd_le32 (raw + entry_pos + 8);

		char name[33];
		memcpy (name, raw + entry_pos + 32, 32);
		name[32] = 0;

		if (!name[0])
			snprintf (name, sizeof (name), "file_%04u.bin", i);

		if (data_offset >= raw_size)
			continue;
		if (data_offset + file_size > raw_size)
			file_size = (u32)(raw_size - data_offset);

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

		char *slash = strrchr (out_path, '/');
		if (slash)
		{
			*slash = 0;
			CreatePath (out_path, true);
			*slash = '/';
		}

		if (!testmode && file_size > 0)
			SaveFile (out_path, 0, 0, raw + data_offset, file_size, 0);
	}

	FREE (raw);
	return ERR_OK;
}

// ----------------------------------------------------------------------------
// 13. PlatinumGames Archive (.pkz / pkz)
// ----------------------------------------------------------------------------
enumError ExtractPKZArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".pkz") && !is_ext_match (arg, ".bin") && !is_ext_match (arg, ".dat"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 32 || memcmp (raw, "pkz\0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 file_count = rd_le32 (raw + 16);
	const u32 offset_file_info = rd_le32 (raw + 20);

	if (!file_count || file_count > 100000 || offset_file_info >= raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	const uint info_size = file_count * 32;
	const uint str_table_pos = offset_file_info + info_size;
	if (str_table_pos > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PKZ:%s (%u files) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, file_count, dest);

	for (uint i = 0; i < file_count; i++)
	{
		const uint entry_pos = offset_file_info + i * 32;
		if (entry_pos + 32 > raw_size)
			break;

		const u64 name_offset = (u64)rd_le32 (raw + entry_pos) | ((u64)rd_le32 (raw + entry_pos + 4) << 32);
		const u64 file_size = (u64)rd_le32 (raw + entry_pos + 8) | ((u64)rd_le32 (raw + entry_pos + 12) << 32);
		const u64 file_offset = (u64)rd_le32 (raw + entry_pos + 16) | ((u64)rd_le32 (raw + entry_pos + 20) << 32);
		const u64 comp_size = (u64)rd_le32 (raw + entry_pos + 24) | ((u64)rd_le32 (raw + entry_pos + 28) << 32);

		char name[PATH_MAX];
		const uint full_name_pos = str_table_pos + (uint)name_offset;
		if (full_name_pos < raw_size)
		{
			const char *s = (const char *)(raw + full_name_pos);
			size_t slen = strnlen (s, sizeof (name) - 1);
			memcpy (name, s, slen);
			name[slen] = 0;
		}
		else
		{
			snprintf (name, sizeof (name), "file_%04u.bin", i);
		}

		if (file_offset >= raw_size)
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

		if (!testmode && comp_size > 0 && (size_t)(file_offset + comp_size) <= raw_size)
		{
			if (comp_size != file_size && file_size > 0)
			{
				// Zstandard decompression (Zstb)
				u8 *decomp = 0;
				uint decomp_sz = 0;
				err = DecodeZSTD (&decomp, &decomp_sz, raw + file_offset, (uint)comp_size);
				if (!err && decomp)
				{
					SaveFile (out_path, 0, 0, decomp, decomp_sz, 0);
					FREE (decomp);
				}
				else
				{
					SaveFile (out_path, 0, 0, raw + file_offset, (uint)comp_size, 0);
				}
			}
			else
			{
				SaveFile (out_path, 0, 0, raw + file_offset, (uint)comp_size, 0);
			}
		}
	}

	FREE (raw);
	return ERR_OK;
}

// ----------------------------------------------------------------------------
// 14. Nintendo Switch Joy-Con Vibration Archive (.vibs)
// ----------------------------------------------------------------------------
enumError ExtractVIBSArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".vibs"))
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

	const u32 version = rd_le32 (raw);
	const u32 num_entries = rd_le32 (raw + 4);
	(void)version;

	if (!num_entries || num_entries > 100000 || 8 + (uint64_t)num_entries * 44 > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT VIBS:%s (%u files) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, num_entries, dest);

	for (uint i = 0; i < num_entries; i++)
	{
		const uint entry_pos = 8 + i * 44;
		char name[25];
		memcpy (name, raw + entry_pos, 24);
		name[24] = 0;

		if (!name[0])
			snprintf (name, sizeof (name), "vibration_%04u.bnvib", i);

		u32 data_len = rd_le32 (raw + entry_pos + 32);
		const u32 data_offset = rd_le32 (raw + entry_pos + 40);

		if (data_offset >= raw_size)
			continue;
		if (data_offset + data_len > raw_size)
			data_len = (u32)(raw_size - data_offset);

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

		char *slash = strrchr (out_path, '/');
		if (slash)
		{
			*slash = 0;
			CreatePath (out_path, true);
			*slash = '/';
		}

		if (!testmode && data_len > 0)
			SaveFile (out_path, 0, 0, raw + data_offset, data_len, 0);
	}

	FREE (raw);
	return ERR_OK;
}

// ----------------------------------------------------------------------------
// 15. PlatinumGames DAT Archive (.dat / DAT\0)
// ----------------------------------------------------------------------------
enumError ExtractPGDATArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".dat") && !is_ext_match (arg, ".pkz") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 24 || memcmp (raw, "DAT\0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 file_count = rd_le32 (raw + 4);
	const u32 offset_file_offset_tbl = rd_le32 (raw + 8);
	const u32 offset_file_ext_tbl = rd_le32 (raw + 12);
	const u32 offset_file_name_tbl = rd_le32 (raw + 16);
	const u32 offset_file_size_tbl = rd_le32 (raw + 20);

	if (!file_count || file_count > 100000 ||
		offset_file_offset_tbl >= raw_size ||
		offset_file_size_tbl >= raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PG-DAT:%s (%u files) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, file_count, dest);

	u32 str_size = 0;
	if (offset_file_name_tbl + 4 <= raw_size)
		str_size = rd_le32 (raw + offset_file_name_tbl);

	for (uint i = 0; i < file_count; i++)
	{
		if (offset_file_offset_tbl + (i + 1) * 4 > raw_size ||
			offset_file_size_tbl + (i + 1) * 4 > raw_size)
			break;

		const u32 offset = rd_le32 (raw + offset_file_offset_tbl + i * 4);
		u32 size = rd_le32 (raw + offset_file_size_tbl + i * 4);

		char name[PATH_MAX];
		if (str_size > 0 && offset_file_name_tbl + 4 + (i + 1) * str_size <= raw_size)
		{
			const char *s = (const char *)(raw + offset_file_name_tbl + 4 + i * str_size);
			size_t slen = strnlen (s, sizeof (name) - 1);
			memcpy (name, s, slen);
			name[slen] = 0;
		}
		else
		{
			char ext[8] = "bin";
			if (offset_file_ext_tbl > 0 && offset_file_ext_tbl + (i + 1) * 4 <= raw_size)
			{
				const char *e = (const char *)(raw + offset_file_ext_tbl + i * 4);
				uint elen = 0;
				while (elen < 4 && e[elen] && (isalnum ((unsigned char)e[elen]) || e[elen] == '_'))
					elen++;
				if (elen > 0)
				{
					memcpy (ext, e, elen);
					ext[elen] = 0;
				}
			}
			snprintf (name, sizeof (name), "file_%04u.%s", i, ext);
		}

		if (offset >= raw_size)
			continue;
		if (offset + size > raw_size)
			size = (u32)(raw_size - offset);

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
			SaveFile (out_path, 0, 0, raw + offset, size, 0);
	}

	FREE (raw);
	return ERR_OK;
}

// ----------------------------------------------------------------------------
// 16. PlatinumGames WT Archive (.wta / WTA )
// ----------------------------------------------------------------------------
enumError ExtractWTAArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".wta") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 32 || memcmp (raw, "WTA ", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// WTA Header:
	// 0x00: Magic ("WTA\x20" or "WTB\x00")
	// 0x04: Version (usually 1)
	// 0x08: Number of textures / files
	// 0x0C: Offset to data offset table (or texture info table)
	// 0x10: Offset to data size table
	// 0x14: Unk table / flags table
	// 0x18: Unk2 table / idx table
	// 0x1C: Texture info table
	const u32 ver_be = rd_be32 (raw + 4);
	const u32 ver_le = rd_le32 (raw + 4);
	const bool big = (ver_be > 0 && ver_be <= 0xFFFF && ver_le > 0xFFFF);

	const u32 file_count = big ? rd_be32 (raw + 8) : rd_le32 (raw + 8);
	const u32 offset_pos_table = big ? rd_be32 (raw + 12) : rd_le32 (raw + 12);
	const u32 offset_size_table = big ? rd_be32 (raw + 16) : rd_le32 (raw + 16);

	if (!file_count || file_count > 100000)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT WTA:%s (%u files, %s-endian) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, file_count,
			big ? "big" : "little", dest);

	for (uint i = 0; i < file_count; i++)
	{
		u32 cur_data_offset = 0;
		u32 comp_sz = 0;
		u32 uncomp_sz = 0;

		if (offset_pos_table >= 32 && offset_pos_table + (i + 1) * 4 <= raw_size &&
		    offset_size_table >= 32 && offset_size_table + (i + 1) * 4 <= raw_size)
		{
			cur_data_offset = big ? rd_be32 (raw + offset_pos_table + i * 4) : rd_le32 (raw + offset_pos_table + i * 4);
			comp_sz = big ? rd_be32 (raw + offset_size_table + i * 4) : rd_le32 (raw + offset_size_table + i * 4);
			uncomp_sz = comp_sz;
		}
		else
		{
			// Fallback: 32-byte descriptor table starting at offset 32
			const uint entry_pos = 32 + i * 32;
			if (entry_pos + 32 > raw_size)
				break;
			uncomp_sz = big ? rd_be32 (raw + entry_pos + 12) : rd_le32 (raw + entry_pos + 12);
			comp_sz = big ? rd_be32 (raw + entry_pos + 16) : rd_le32 (raw + entry_pos + 16);
			cur_data_offset = 32 + file_count * 32;
		}

		char name[PATH_MAX];
		snprintf (name, sizeof (name), "texture_%04u.bin", i);

		if (cur_data_offset < raw_size && comp_sz > 0)
		{
			if (cur_data_offset + comp_sz > raw_size)
				comp_sz = (u32)(raw_size - cur_data_offset);

			char out_path[PATH_MAX];
			snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

			char *slash = strrchr (out_path, '/');
			if (slash)
			{
				*slash = 0;
				CreatePath (out_path, true);
				*slash = '/';
			}

			if (!testmode)
			{
				if (comp_sz != uncomp_sz && comp_sz >= 2 &&
					raw[cur_data_offset] == 0x78)
				{
					u8 *decomp = 0;
					uint decomp_sz = 0;
					err = DecodeZlibGrow (&decomp, &decomp_sz, raw + cur_data_offset, comp_sz);
					if (!err && decomp)
					{
						SaveFile (out_path, 0, 0, decomp, decomp_sz, 0);
						FREE (decomp);
					}
					else
					{
						SaveFile (out_path, 0, 0, raw + cur_data_offset, comp_sz, 0);
					}
				}
				else
				{
					SaveFile (out_path, 0, 0, raw + cur_data_offset, comp_sz, 0);
				}
			}
		}
	}

	FREE (raw);
	return ERR_OK;
}

enumError CreateZLARCArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	const u32 header_sz = 4 + n_entries * 4;
	u32 descriptors_sz = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = sorted[i].name ? sorted[i].name : "";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;
		descriptors_sz += 12 + strlen (name) + 1;
	}

	const u32 data_start = header_sz + descriptors_sz;
	u32 total_data_sz = 0;
	for (uint i = 0; i < n_entries; i++)
		total_data_sz += sorted[i].size;

	const u32 uncomp_sz = data_start + total_data_sz;
	u8 *uncomp = CALLOC (uncomp_sz, 1);
	if (!uncomp)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	wr_be32 (uncomp, n_entries);

	u32 cur_desc_off = header_sz;
	u32 cur_data_off = 0;

	for (uint i = 0; i < n_entries; i++)
	{
		wr_be32 (uncomp + 4 + i * 4, cur_desc_off);

		ccp name = sorted[i].name ? sorted[i].name : "";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;
		const uint nlen = (uint)strlen (name) + 1;

		wr_be32 (uncomp + cur_desc_off, cur_data_off);
		wr_be32 (uncomp + cur_desc_off + 4, sorted[i].size);
		wr_be32 (uncomp + cur_desc_off + 8, nlen);
		memcpy (uncomp + cur_desc_off + 12, name, nlen);

		if (sorted[i].data && sorted[i].size > 0)
			memcpy (uncomp + data_start + cur_data_off, sorted[i].data, sorted[i].size);

		cur_desc_off += 12 + nlen;
		cur_data_off += sorted[i].size;
	}

	FREE (sorted);

	uLongf bound = compressBound (uncomp_sz);
	u8 *comp = MALLOC (bound);
	uLongf comp_len = bound;
	if (compress (comp, &comp_len, uncomp, uncomp_sz) != Z_OK)
	{
		FREE (uncomp);
		FREE (comp);
		return ERR_CANT_CREATE;
	}

	FREE (uncomp);
	*dest = comp;
	*dest_size = (uint)comp_len;
	return ERR_OK;
}
