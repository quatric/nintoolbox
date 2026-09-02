// SPDX-License-Identifier: GPL-2.0+
#include "lib-nintendo-archives.h"
#include "lib-nintendo.h"
#include "lib-szs.h"
#include "lib-std.h"
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
	if (8 + count * 16 > raw_size)
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
	if (!is_ext_match (arg, ".mdr") && !is_ext_match (arg, ".bin"))
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
	if (!is_ext_match (arg, ".pvol") && !is_ext_match (arg, ".bin"))
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

	const u32 fcount = rd_le32 (raw);
	if (fcount < 2 || 4 + (fcount - 1) * 8 > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
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
	if (0x10 + resource_count * 0x30 > raw_size)
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
	if (chunks_offset + 8 + chunk_count * 20 > raw_size)
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
	if (!count || 4 + count * 4 > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Validate first offset
	const u32 first_off = rd_be32 (raw + 4);
	if (first_off < 4 + count * 4 || first_off + 12 > raw_size)
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
