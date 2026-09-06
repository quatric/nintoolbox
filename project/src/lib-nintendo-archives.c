// SPDX-License-Identifier: GPL-2.0+
#include "lib-nintendo-archives.h"
#include "lib-nintendo.h"
#include "lib-image.h" // TPL headers, for re-emitting PTLG textures
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
	// .mkgpdx is the disambiguating extension CREATE writes, since .pac is
	// already claimed by the HAL Laboratory / Game Arts container.
	if (!is_ext_match (arg, ".pac") && !is_ext_match (arg, ".bin")
		&& !is_ext_match (arg, ".mkgpdx"))
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
		fprintf (stdlog, "%s%sEXTRACT GFPAK:%s (%u files) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, file_count, dest);

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

// ----------------------------------------------------------------------------
// 18. Nintendo Binary Audio Resource Archive (.bars / BARS)
// ----------------------------------------------------------------------------
enumError ExtractBARSArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".bars") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 16 || memcmp (raw, "BARS", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// BARS Header:
	// 0x00: Magic ("BARS", 4 bytes)
	// 0x04: u32 file_size
	// 0x08: u16 BOM (0xFEFF for big-endian, 0xFFFE for little-endian)
	// 0x0A: u16 version
	// 0x0C: u32 asset_count
	const u16 bom = rd_be16 (raw + 8);
	const bool big = (bom == 0xFEFF);

	const u32 asset_count = big ? rd_be32 (raw + 12) : rd_le32 (raw + 12);
	if (!asset_count || asset_count > 100000)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BARS:%s (%u assets, %s-endian) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, asset_count,
			big ? "big" : "little", dest);

	const uint hash_table_offset = 16;
	const uint offset_pairs_table = hash_table_offset + asset_count * 4;

	if (offset_pairs_table + asset_count * 8 > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	for (uint i = 0; i < asset_count; i++)
	{
		const uint pair_pos = offset_pairs_table + i * 8;
		const u32 amta_offset = big ? rd_be32 (raw + pair_pos) : rd_le32 (raw + pair_pos);
		const u32 audio_offset = big ? rd_be32 (raw + pair_pos + 4) : rd_le32 (raw + pair_pos + 4);

		char name[PATH_MAX];
		bool name_found = false;

		// Check AMTA metadata chunk for filename
		if (amta_offset + 0x30 <= raw_size && !memcmp (raw + amta_offset, "AMTA", 4))
		{
			const u32 name_rel_ptr = big ? rd_be32 (raw + amta_offset + 0x24) : rd_le32 (raw + amta_offset + 0x24);
			const uint name_abs = amta_offset + 0x24 + name_rel_ptr;
			if (name_abs < raw_size)
			{
				const char *str = (const char *)(raw + name_abs);
				size_t slen = strnlen (str, sizeof (name) - 1);
				if (slen > 0)
				{
					memcpy (name, str, slen);
					name[slen] = 0;
					name_found = true;
				}
			}
		}

		if (!name_found)
			snprintf (name, sizeof (name), "audio_%04u", i);

		// If audio_offset is valid, extract the audio asset (BWAV / BFWAV / etc.)
		if (audio_offset != 0xFFFFFFFF && audio_offset < raw_size)
		{
			// Determine audio asset size: BWAV / BFWAV header contains size at offset 8 or 12
			u32 audio_sz = 0;
			ccp ext = ".bwav";
			if (audio_offset + 16 <= raw_size)
			{
				if (!memcmp (raw + audio_offset, "BWAV", 4))
				{
					ext = ".bwav";
					audio_sz = big ? rd_be32 (raw + audio_offset + 8) : rd_le32 (raw + audio_offset + 8);
				}
				else if (!memcmp (raw + audio_offset, "FWAV", 4))
				{
					ext = ".bfwav";
					audio_sz = big ? rd_be32 (raw + audio_offset + 8) : rd_le32 (raw + audio_offset + 8);
				}
			}

			if (audio_sz == 0 || audio_offset + audio_sz > raw_size)
			{
				// Bound by raw_size or next asset/offset
				audio_sz = (u32)(raw_size - audio_offset);
			}

			char out_path[PATH_MAX];
			// Avoid double extension if name already has one
			if (strchr (name, '.'))
				snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);
			else
				snprintf (out_path, sizeof (out_path), "%s/%s%s", dest, name, ext);

			char *slash = strrchr (out_path, '/');
			if (slash)
			{
				*slash = 0;
				CreatePath (out_path, true);
				*slash = '/';
			}

			if (!testmode && audio_sz > 0)
				SaveFile (out_path, 0, 0, raw + audio_offset, audio_sz, 0);
		}

		// Also extract AMTA metadata chunk alongside if available
		if (amta_offset < raw_size && amta_offset + 12 <= raw_size && !memcmp (raw + amta_offset, "AMTA", 4))
		{
			u32 amta_sz = big ? rd_be32 (raw + amta_offset + 8) : rd_le32 (raw + amta_offset + 8);
			if (amta_sz == 0 || amta_offset + amta_sz > raw_size)
				amta_sz = (u32)(raw_size - amta_offset);

			char out_amta[PATH_MAX];
			char clean_name[PATH_MAX];
			snprintf (clean_name, sizeof (clean_name), "%s", name);
			char *dot = strrchr (clean_name, '.');
			if (dot)
				*dot = 0;
			snprintf (out_amta, sizeof (out_amta), "%s/%s.amta", dest, clean_name);

			char *slash = strrchr (out_amta, '/');
			if (slash)
			{
				*slash = 0;
				CreatePath (out_amta, true);
				*slash = '/';
			}

			if (!testmode && amta_sz > 0)
				SaveFile (out_amta, 0, 0, raw + amta_offset, amta_sz, 0);
		}
	}

	FREE (raw);
	return ERR_OK;
}

// Extract Next Level Games Dictionary Archive (.dict / LM2 / LM3 / Punch-Out!!)
enumError ExtractNLGDictArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".dict") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 16)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 m_be = rd_be32 (raw);
	const u32 m_le = rd_le32 (raw);

	// Supported magics:
	// 0x5824F3A9: LM2 / LM3
	// 0xA9F32458: Punch-Out!! Wii
	bool is_lm = (m_be == 0x5824F3A9 || m_le == 0x5824F3A9);
	bool is_po = (m_be == 0xA9F32458 || m_le == 0xA9F32458);

	if (!is_lm && !is_po)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Determine companion .data file
	char data_path[PATH_MAX];
	snprintf (data_path, sizeof (data_path), "%s", arg);
	char *dot = strrchr (data_path, '.');
	if (dot && !strcasecmp (dot, ".dict"))
		strcpy (dot, ".data");
	else
		snprintf (data_path, sizeof (data_path), "%s.data", arg);

	u8 *data_raw = 0;
	size_t data_raw_size = 0;
	LoadFileAlloc (data_path, 0, 0, &data_raw, &data_raw_size, 0, 0, 0, false);

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	uint extracted_count = 0;

	if (is_lm)
	{
		// LM2 / LM3 Dictionary format
		// Check LM3 indicator at offset 12: 0x78340300 (or BE equivalent)
		const bool is_lm3 = (raw_size >= 16 && (rd_be32 (raw + 12) == 0x78340300 || rd_le32 (raw + 12) == 0x78340300));
		const bool is_compressed = (raw[6] == 1);

		uint num_files = 0;
		uint file_table_offset = 0;

		if (is_lm3)
		{
			num_files = raw[16];
			const uint num_chunk_infos = raw[17];
			file_table_offset = 20 + num_chunk_infos * 24;
		}
		else
		{
			num_files = rd_le32 (raw + 8);
			if (num_files == 0 || num_files > 100000)
				num_files = rd_be32 (raw + 8);
			file_table_offset = 0x2C + num_files;
		}

		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT %s:%s (%u files) -> %s/\n",
				verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
				is_lm3 ? "LM3-DICT" : "LM2-DICT", arg, num_files, dest);

		for (uint i = 0; i < num_files; i++)
		{
			const uint eoff = file_table_offset + i * 16;
			if (eoff + 16 > raw_size)
				break;

			const u32 offset = rd_le32 (raw + eoff);
			const u32 decomp_size = rd_le32 (raw + eoff + 4);
			const u32 comp_size = rd_le32 (raw + eoff + 8);

			if (decomp_size == 0)
				continue;

			// Source buffer can come from .data file if present, or from .dict itself if within raw_size
			const u8 *src = 0;
			size_t src_avail = 0;
			if (data_raw && offset < data_raw_size)
			{
				src = data_raw + offset;
				src_avail = data_raw_size - offset;
			}
			else if (offset < raw_size)
			{
				src = raw + offset;
				src_avail = raw_size - offset;
			}

			if (!src)
				continue;

			char out_path[PATH_MAX];
			snprintf (out_path, sizeof (out_path), "%s/file_%04u.bin", dest, i);

			if (!testmode)
			{
				if (is_compressed && comp_size > 0 && src_avail >= comp_size)
				{
					// Check zlib header
					if (comp_size >= 2 && (src[0] == 0x78 && (src[1] == 0x9c || src[1] == 0xda || src[1] == 0x01 || src[1] == 0x5e)))
					{
						u8 *decomp = MALLOC (decomp_size);
						if (decomp)
						{
							uLongf dest_len = decomp_size;
							if (uncompress (decomp, &dest_len, src, comp_size) == Z_OK)
							{
								SaveFile (out_path, 0, 0, decomp, (uint)dest_len, 0);
								extracted_count++;
								FREE (decomp);
								continue;
							}
							FREE (decomp);
						}
					}
					// Raw dump if decompression fails or not compressed
					SaveFile (out_path, 0, 0, src, comp_size, 0);
					extracted_count++;
				}
				else
				{
					const u32 sz = decomp_size <= src_avail ? decomp_size : (u32)src_avail;
					SaveFile (out_path, 0, 0, src, sz, 0);
					extracted_count++;
				}
			}
			else
				extracted_count++;
		}
	}
	else if (is_po)
	{
		// Punch-Out!! Wii Dictionary
		const bool is_compressed = (raw[6] == 1);
		const u32 num_files = rd_be32 (raw + 16);
		const u32 file_table_size = rd_be32 (raw + 20);

		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT PO-DICT:%s (%u files) -> %s/\n",
				verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, num_files, dest);

		// Calculate block table offset
		u32 cur_blk_off = 0;
		u32 blk_offsets[8] = { 0 };
		u32 blk_sizes[8] = { 0 };
		for (uint b = 0; b < 8; b++)
		{
			const uint boff = 24 + b * 8;
			if (boff + 8 <= raw_size)
			{
				blk_offsets[b] = cur_blk_off;
				blk_sizes[b] = rd_be32 (raw + boff);
				cur_blk_off += blk_sizes[b];
			}
		}
		const u32 file_table_offset = cur_blk_off;

		// File table is in .data file at file_table_offset, or in .dict if present
		const u8 *tbl_src = 0;
		size_t tbl_avail = 0;
		if (data_raw && file_table_offset < data_raw_size)
		{
			tbl_src = data_raw + file_table_offset;
			tbl_avail = data_raw_size - file_table_offset;
		}
		else if (file_table_offset < raw_size)
		{
			tbl_src = raw + file_table_offset;
			tbl_avail = raw_size - file_table_offset;
		}

		if (tbl_src && num_files > 0)
		{
			for (uint i = 0; i < num_files; i++)
			{
				const uint eoff = i * 10;
				if (eoff + 10 > tbl_avail)
					break;

				const u8 chunk_flags = tbl_src[eoff];
				const u16 chunk_type = rd_be16 (tbl_src + eoff + 2);
				const u32 chunk_sz = rd_be32 (tbl_src + eoff + 4);
				const u32 chunk_off = rd_be32 (tbl_src + eoff + 8);

				int blk_idx = -1;
				if (chunk_flags == 0x12) blk_idx = 0;
				else if (chunk_flags == 0x25) blk_idx = 1;
				else if (chunk_flags == 2) blk_idx = 2;
				else if (chunk_flags == 0x42) blk_idx = 3;
				else if (chunk_flags == 3) blk_idx = 0;
				else
				{
					const u8 bf = chunk_flags >> 4;
					if (bf < 8) blk_idx = bf;
				}

				if (blk_idx < 0 || blk_idx >= 8 || chunk_sz == 0)
					continue;

				const u32 abs_off = blk_offsets[blk_idx] + chunk_off;
				const u8 *f_src = 0;
				size_t f_avail = 0;
				if (data_raw && abs_off < data_raw_size)
				{
					f_src = data_raw + abs_off;
					f_avail = data_raw_size - abs_off;
				}
				else if (abs_off < raw_size)
				{
					f_src = raw + abs_off;
					f_avail = raw_size - abs_off;
				}

				if (!f_src)
					continue;

				const u32 to_write = chunk_sz <= f_avail ? chunk_sz : (u32)f_avail;
				char out_path[PATH_MAX];
				snprintf (out_path, sizeof (out_path), "%s/chunk_%04u_type_%04x.bin", dest, i, chunk_type);

				if (!testmode && to_write > 0)
					SaveFile (out_path, 0, 0, f_src, to_write, 0);

				extracted_count++;
			}
		}
	}

	if (data_raw)
		FREE (data_raw);
	FREE (raw);

	return ERR_OK;
}

// Extract Next Level Games Texture To Go (.txtg / 6PK0)
enumError ExtractTXTGArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".txtg") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 0x50 || memcmp (raw + 4, "6PK0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// TXTG Header:
	// 0x00: u16 HeaderSize (usually 0x50)
	// 0x02: u16 Version (usually 0x11)
	// 0x04: 4 bytes Magic "6PK0"
	// 0x08: u16 Width
	// 0x0A: u16 Height
	// 0x0C: u16 Depth
	// 0x0E: u8 MipCount
	// 0x0F: u8 Unknown1
	// 0x10: u8 Unknown2
	// 0x11: u8 Padding
	// 0x12: u8 FormatFlag
	// 0x13: u32 FormatSetting (or padding)
	// 0x44: u16 Format
	const u16 header_size = rd_le16 (raw);
	const u16 width = rd_le16 (raw + 8);
	const u16 height = rd_le16 (raw + 10);
	const u16 depth_cnt = rd_le16 (raw + 12);
	const u8 mip_count = raw[14];
	const u16 fmt = rd_le16 (raw + 0x44);

	const uint total_surfaces = (depth_cnt ? depth_cnt : 1) * (mip_count ? mip_count : 1);
	if (total_surfaces == 0 || total_surfaces > 1000)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT TXTG:%s (%ux%u, fmt 0x%04x, %u surfaces) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, width, height, fmt, total_surfaces, dest);

	// Surface headers follow header_size
	// First table: total_surfaces * 4 bytes (u16 ArrayLevel, u8 MipLevel, u8 unk)
	// Second table: total_surfaces * 8 bytes (u32 Size, u32 unk)
	const uint table1_off = header_size;
	const uint table2_off = table1_off + total_surfaces * 4;
	const uint data_start = table2_off + total_surfaces * 8;

	if (data_start > raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	u32 cur_data_off = data_start;
	for (uint i = 0; i < total_surfaces; i++)
	{
		const u16 array_lvl = rd_le16 (raw + table1_off + i * 4);
		const u8 mip_lvl = raw[table1_off + i * 4 + 2];
		const u32 surf_sz = rd_le32 (raw + table2_off + i * 8);

		if (cur_data_off >= raw_size)
			break;

		const u32 to_write = cur_data_off + surf_sz <= raw_size ? surf_sz : (u32)(raw_size - cur_data_off);

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/surface_a%u_m%u.bin", dest, array_lvl, mip_lvl);

		if (!testmode && to_write > 0)
			SaveFile (out_path, 0, 0, raw + cur_data_off, to_write, 0);

		cur_data_off = (cur_data_off + surf_sz + 15) & ~15;
	}

	FREE (raw);
	return ERR_OK;
}

// Extract Nintendo 3DS RomFS Archive (.romfs / IVFC)
static void RomFS_ReadDirectories (
	const u8 *raw, size_t raw_size,
	uint dir_start, uint file_start, uint data_start,
	uint cur_dir_off, ccp current_path, ccp dest)
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
		RomFS_ReadDirectories (raw, raw_size, dir_start, file_start, data_start, first_child_off, new_path, dest);

	// Read next sibling directories
	if (next_sibling_off != 0xFFFFFFFF)
		RomFS_ReadDirectories (raw, raw_size, dir_start, file_start, data_start, next_sibling_off, current_path, dest);
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
		fprintf (stdlog, "%s%sEXTRACT ROMFS:%s -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);

	RomFS_ReadDirectories (raw, raw_size, dir_start, file_start, data_start, 0, "", dest);

	FREE (raw);
	return ERR_OK;
}

// Extract Nintendo Switch XTX Texture Container (.xtx / DFvN)
enumError ExtractXTXArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".xtx") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 16 || memcmp (raw, "DFvN", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// XTX Header (16 bytes):
	// 0x00: "DFvN"
	// 0x04: u32 HeaderSize
	// 0x08: u32 MajorVersion
	// 0x0C: u32 MinorVersion
	const u32 header_size = rd_le32 (raw + 4);
	if (header_size < 16 || header_size >= raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	uint block_pos = header_size;
	uint image_idx = 0;

	// Loop through blocks (HBvN)
	while (block_pos + 32 <= raw_size)
	{
		if (memcmp (raw + block_pos, "HBvN", 4))
			break;

		const u32 block_size = rd_le32 (raw + block_pos + 4);
		const u64 data_size = rd_le64 (raw + block_pos + 8);
		const s64 data_offset = (s64)rd_le64 (raw + block_pos + 16);
		const u32 block_type = rd_le32 (raw + block_pos + 24);

		// BlockType 3 is Texture Data block
		if (block_type == 3 && data_size > 0)
		{
			const s64 abs_payload = (s64)block_pos + data_offset;
			if (abs_payload >= 0 && (size_t)abs_payload + data_size <= raw_size)
			{
				char out_file[PATH_MAX];
				snprintf (out_file, sizeof (out_file), "%s/texture_%04u.bin", dest, image_idx++);

				if (!testmode)
					SaveFile (out_file, 0, 0, raw + abs_payload, (uint)data_size, 0);
			}
		}

		if (block_size == 0)
			break;
		block_pos += block_size;
	}

	FREE (raw);
	return ERR_OK;
}

// Extract Koei Tecmo / Gust Texture Volume Archive (.tvol)
enumError ExtractTVOLArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".tvol"))
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

	const u32 num_textures = rd_le32 (raw);
	if (num_textures == 0 || num_textures > 10000 || 4 + num_textures * 8 > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	for (uint i = 0; i < num_textures; i++)
	{
		const u32 offset = rd_le32 (raw + 4 + i * 8);
		const u32 size = rd_le32 (raw + 4 + i * 8 + 4);

		if (size == 0 || offset >= raw_size)
			continue;

		const u32 to_write = offset + size <= raw_size ? size : (u32)(raw_size - offset);

		// Name is stored at offset as null-terminated string (up to 48 bytes)
		char name[64] = "";
		if (offset + 48 <= raw_size)
		{
			const char *nptr = (const char *)(raw + offset);
			size_t nlen = strnlen (nptr, 47);
			if (nlen > 0)
			{
				memcpy (name, nptr, nlen);
				name[nlen] = 0;
			}
		}

		char out_file[PATH_MAX];
		if (*name)
			snprintf (out_file, sizeof (out_file), "%s/%s.bin", dest, name);
		else
			snprintf (out_file, sizeof (out_file), "%s/tex_%04u.bin", dest, i);

		if (!testmode && to_write > 0)
			SaveFile (out_file, 0, 0, raw + offset, to_write, 0);
	}

	FREE (raw);
	return ERR_OK;
}

// Extract Nintendo Switch MTXT Texture Archive (.mtxt / MTXT)
enumError ExtractMTXTArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".mtxt") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 16 || memcmp (raw, "MTXT", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	// MTXT format:
	// 0x00: "MTXT"
	// 0x04: u32 Flags
	// 0x08: Gzip compressed stream or uncompressed payload containing inner texture/XTX data.
	// We decompress from offset 8 using zlib inflate (with gzip support).
	z_stream strm;
	memset (&strm, 0, sizeof (strm));
	strm.next_in = (Bytef *)(raw + 8);
	strm.avail_in = raw_size - 8;

	uint decomp_cap = 64 * 1024;
	u8 *decomp = MALLOC (decomp_cap);
	bool decompressed = false;

	if (decomp && (inflateInit2 (&strm, 15 + 32) == Z_OK || inflateInit2 (&strm, -15) == Z_OK))
	{
		for (;;)
		{
			strm.next_out = decomp + strm.total_out;
			strm.avail_out = decomp_cap - strm.total_out;

			int ret = inflate (&strm, Z_NO_FLUSH);
			if (ret == Z_STREAM_END)
			{
				decompressed = true;
				break;
			}
			if (ret != Z_OK)
				break;

			if (strm.avail_out == 0)
			{
				decomp_cap *= 2;
				u8 *n = REALLOC (decomp, decomp_cap);
				if (!n)
					break;
				decomp = n;
			}
		}
		inflateEnd (&strm);
	}

	if (decompressed && strm.total_out > 0)
	{
		// Extracted decompressed payload
		const uint decomp_size = (uint)strm.total_out;
		// Check if payload contains inner XTX ("DFvN")
		uint xtx_off = 0;
		bool has_xtx = false;
		for (uint i = 0; i + 4 <= decomp_size; i += 4)
		{
			if (!memcmp (decomp + i, "DFvN", 4))
			{
				xtx_off = i;
				has_xtx = true;
				break;
			}
		}

		if (has_xtx && xtx_off + 16 <= decomp_size)
		{
			char out_xtx[PATH_MAX];
			snprintf (out_xtx, sizeof (out_xtx), "%s/texture.xtx", dest);
			if (!testmode)
				SaveFile (out_xtx, 0, 0, decomp + xtx_off, decomp_size - xtx_off, 0);

			// Also extract any texture data block inside inner XTX
			const u32 header_size = rd_le32 (decomp + xtx_off + 4);
			if (header_size >= 16 && xtx_off + header_size < decomp_size)
			{
				uint bpos = xtx_off + header_size;
				uint img_idx = 0;
				while (bpos + 32 <= decomp_size)
				{
					if (memcmp (decomp + bpos, "HBvN", 4))
						break;
					const u32 block_size = rd_le32 (decomp + bpos + 4);
					const u64 data_size = rd_le64 (decomp + bpos + 8);
					const s64 data_offset = (s64)rd_le64 (decomp + bpos + 16);
					const u32 block_type = rd_le32 (decomp + bpos + 24);

					if (block_type == 3 && data_size > 0)
					{
						const s64 abs_payload = (s64)bpos + data_offset;
						if (abs_payload >= 0 && (size_t)abs_payload + data_size <= decomp_size)
						{
							char out_bin[PATH_MAX];
							snprintf (out_bin, sizeof (out_bin), "%s/surface_%04u.bin", dest, img_idx++);
							if (!testmode)
								SaveFile (out_bin, 0, 0, decomp + abs_payload, (uint)data_size, 0);
						}
					}
					if (block_size == 0)
						break;
					bpos += block_size;
				}
			}
		}
		else
		{
			char out_bin[PATH_MAX];
			snprintf (out_bin, sizeof (out_bin), "%s/payload.bin", dest);
			if (!testmode)
				SaveFile (out_bin, 0, 0, decomp, decomp_size, 0);
		}
		FREE (decomp);
	}
	else
	{
		if (decomp)
			FREE (decomp);
		// If decompression failed, save raw payload after 8-byte header
		if (raw_size > 8)
		{
			char out_bin[PATH_MAX];
			snprintf (out_bin, sizeof (out_bin), "%s/payload.bin", dest);
			if (!testmode)
				SaveFile (out_bin, 0, 0, raw + 8, (uint)(raw_size - 8), 0);
		}
	}

	FREE (raw);
	return ERR_OK;
}

// Extract Pokemon Mystery Dungeon Resource Container (.sir0 / SIR0)
enumError ExtractSIR0Archive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".sir0") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 16 || memcmp (raw, "SIR0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	// SIR0 Header:
	// 0x00: "SIR0" (4 bytes)
	// 0x04: u32 SubHeaderOffset / DataPointer
	// 0x08: u32 PointerOffsetsOffset
	// 0x0C: u32 Magic / Padding (usually 0)
	const u32 subheader_offset = rd_le32 (raw + 4);
	const u32 pointer_offsets = rd_le32 (raw + 8);

	// Extract primary data segment (from 0x10 to subheader_offset)
	// and subheader segment (from subheader_offset to pointer_offsets)
	u32 data_end = (subheader_offset >= 0x10 && subheader_offset <= raw_size) ? subheader_offset : (u32)raw_size;
	u32 sub_end = (pointer_offsets >= data_end && pointer_offsets <= raw_size) ? pointer_offsets : (u32)raw_size;

	if (subheader_offset >= 0x10 && subheader_offset < raw_size && sub_end > subheader_offset)
	{
		char sub_file[PATH_MAX];
		snprintf (sub_file, sizeof (sub_file), "%s/subheader.bin", dest);
		if (!testmode)
			SaveFile (sub_file, 0, 0, raw + subheader_offset, sub_end - subheader_offset, 0);
	}

	char out_file[PATH_MAX];
	snprintf (out_file, sizeof (out_file), "%s/data.bin", dest);
	if (!testmode && data_end > 0x10)
		SaveFile (out_file, 0, 0, raw + 0x10, data_end - 0x10, 0);

	FREE (raw);
	return ERR_OK;
}

// ----------------------------------------------------------------------------
// Next Level Games PTLG texture container (.glt / .rlt)
//
// Big-endian throughout. Layout, cross-checked against KillzXGaming's
// StrikersRLT.cs (Switch-Toolbox, MIT) and the retail corpora of Super Mario
// Strikers (GameCube .glt) and Mario Strikers Charged (Wii .rlt):
//
//   0x00  "PTLG"
//   0x04  u32 texture count
//   0x08  u32 unknown  (0 on GameCube)
//   0x0C  u32 padding
//         one u32 of padding follows on some builds: if the next word reads
//         as 0, the entry table starts 16 bytes later, else 4 bytes earlier.
//   ...   texture count * { u32 hash, u32 image offset, u32 section size,
//                           u32 unknown }, offsets relative to table end
//   ...   per texture: u32 mip count, u32 unk, u8 unk, u8 format, u8 unk,
//                      u8 unk, u16 width, u16 height, u16 unk, 3 * u32 unk,
//                      then the raw GX pixel data
//
// The pixel data is plain GX texture data in the same formats TPL wraps, so
// each texture is re-emitted as a standalone TPL rather than a raw blob:
// that makes it directly usable with `wimgt DECODE tex.tpl --dest tex.png`
// instead of needing the dimensions and format carried out of band.
// ----------------------------------------------------------------------------

// PTLG format byte -> image_format_t (StrikersRLT.cs FormatList).
static image_format_t ptlg_image_format (u8 format)
{
	switch (format)
	{
		case 0x2:
			return IMG_I4;
		case 0x3:
			return IMG_I8;
		case 0x4:
			return IMG_IA4;
		case 0x5:
			return IMG_RGB5A3;
		case 0x6:
			return IMG_CMPR;
		case 0x7:
			return IMG_RGB565;
		case 0x8:
			return IMG_RGBA32;
		default:
			return IMG_INVALID;
	}
}

enumError ExtractPTLGArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".glt") && !is_ext_match (arg, ".rlt"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (raw_size < 0x20 || memcmp (raw, "PTLG", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 n_tex = rd_be32 (raw + 4);
	if (!n_tex || n_tex > 0x10000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	// Word at 0x08 is zero on GameCube (.glt) and a hash on Wii (.rlt);
	// StrikersRLT.cs uses the same discriminator.
	const bool is_gc = rd_be32 (raw + 8) == 0;

	// See the layout note above: some builds carry an extra padding word
	// before the entry table.
	u32 tab_off = rd_be32 (raw + 0x10) == 0 ? 0x20 : 0x10;
	if ((u64) tab_off + (u64) n_tex * 16 > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 data_base = tab_off + n_tex * 16;

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	uint written = 0;
	for (u32 i = 0; i < n_tex; i++)
	{
		const u8 *ent = raw + tab_off + i * 16;
		const u32 hash = rd_be32 (ent);
		const u32 img_off = rd_be32 (ent + 4);
		const u32 sect_size = rd_be32 (ent + 8);

		const u64 abs = (u64) data_base + img_off;
		if (abs + 0x20 > raw_size || !sect_size || abs + sect_size > raw_size)
			continue;

		// Per-texture header, common prefix:
		//   0x00 u32 mip count, 0x04 u32 unknown (2), 0x08 u8 unknown (5),
		//   0x09 u8 format, 0x0a u8 unknown (5), 0x0b u8 unknown
		// then width/height, then the GX pixel data including the whole mip
		// chain. GameCube packs width/height at 0x0c/0x0e for a 16-byte
		// header; the Wii build pads two bytes first, putting them at
		// 0x0e/0x10 and padding the header out to 32.
		//
		// Both sizes are confirmed arithmetically rather than assumed: for
		// every CMPR texture in Mario.glt / extratextures.rlt the summed mip
		// chain equals sectionSize minus exactly 16 (GameCube) or 32 (Wii).
		// StrikersRLT.cs instead computes a 30/32-byte header for both,
		// which on GameCube eats 14 bytes of pixel data and leaves the image
		// misaligned (correct dimensions, scrambled colours).
		const u8 *th = raw + abs;
		const u32 mipcount = rd_be32 (th);
		const u8 format = th[9];
		const u16 width = is_gc ? rd_be16 (th + 12) : rd_be16 (th + 14);
		const u16 height = is_gc ? rd_be16 (th + 14) : rd_be16 (th + 16);
		const u32 hdr = is_gc ? 16 : 32;

		const image_format_t iform = ptlg_image_format (format);
		if (iform == IMG_INVALID || !width || !height || hdr >= sect_size)
			continue;

		const u32 img_size = sect_size - hdr;
		if (abs + hdr + img_size > raw_size)
			continue;

		// Wrap the GX pixel data in a minimal single-image TPL.
		const u32 tpl_hdr = sizeof (tpl_header_t);		 // 0x0c
		const u32 tpl_tab = tpl_hdr + sizeof (tpl_imgtab_t); // 0x14
		const u32 tpl_data = tpl_tab + sizeof (tpl_img_header_t);
		u8 *tpl = CALLOC (tpl_data + img_size, 1);

		write_be32 (tpl, TPL_MAGIC_NUM);
		write_be32 (tpl + 4, 1);
		write_be32 (tpl + 8, tpl_hdr);
		write_be32 (tpl + tpl_hdr, tpl_tab); // image_off
		write_be32 (tpl + tpl_hdr + 4, 0);	 // palette_off
		write_be16 (tpl + tpl_tab, height);
		write_be16 (tpl + tpl_tab + 2, width);
		write_be32 (tpl + tpl_tab + 4, iform);
		write_be32 (tpl + tpl_tab + 8, tpl_data);
		write_be32 (tpl + tpl_tab + 20, 1); // min_filter
		write_be32 (tpl + tpl_tab + 24, 1); // mag_filter
		memcpy (tpl + tpl_data, raw + abs + hdr, img_size);

		char out[PATH_MAX];
		snprintf (out, sizeof (out), "%s/%08x.tpl", dest, hash);
		if (!testmode)
			SaveFile (out, 0, 0, tpl, tpl_data + img_size, 0);
		FREE (tpl);
		written++;

		if (verbose > 0)
			fprintf (stdlog, "  PTLG texture %08x: %ux%u fmt=%u mips=%u\n", hash, width, height,
				format, mipcount);
	}

	FREE (raw);
	return written ? ERR_OK : ERR_NOTHING_TO_DO;
}

enumError CreatePTLGArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries, bool is_gc)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	// Calculate section sizes
	const u32 tab_off = 0x10;
	const u32 table_bytes = n_entries * 16;
	const u32 data_base = tab_off + table_bytes;
	const u32 th_size = is_gc ? 16 : 32;

	// Pass 1: compute offsets and total size
	u32 cur_img_off = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		const nintendo_sarc_entry_t *e = &entries[i];
		u32 payload_sz = 0;
		if (e->data && e->size >= 0x28 && memcmp (e->data, "\x00\x20\xaf\x30", 4) == 0)
		{
			const tpl_header_t *th = (const tpl_header_t *)e->data;
			u32 n_img = be32 (&th->n_image);
			u32 img_tab_off = be32 (&th->imgtab_off);
			if (n_img > 0 && img_tab_off + 4 <= e->size)
			{
				u32 img_hdr_off = be32 (e->data + img_tab_off);
				if (img_hdr_off + sizeof (tpl_img_header_t) <= e->size)
				{
					const tpl_img_header_t *ti = (const tpl_img_header_t *)(e->data + img_hdr_off);
					u32 d_off = be32 (&ti->data_off);
					if (d_off < e->size)
						payload_sz = e->size - d_off;
				}
			}
		}
		if (!payload_sz)
			payload_sz = e->size;

		u32 sect_size = th_size + payload_sz;
		cur_img_off += sect_size;
	}

	const u32 total_size = data_base + cur_img_off;
	u8 *buf = CALLOC (1, total_size);
	if (!buf)
		return ERR_OUT_OF_MEMORY;

	// Header
	memcpy (buf, "PTLG", 4);
	write_be32 (buf + 4, n_entries);
	write_be32 (buf + 8, is_gc ? 0 : 0xC31808CF);
	write_be32 (buf + 12, 0);

	// Pass 2: serialize entries and textures
	cur_img_off = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		const nintendo_sarc_entry_t *e = &entries[i];
		u32 hash = 0;
		if (e->name)
		{
			// If filename is hex like "abcd1234.tpl", parse it
			char *endp = 0;
			u32 hval = (u32)strtoul (e->name, &endp, 16);
			if (endp && (*endp == '.' || *endp == 0))
				hash = hval;
			else
				hash = CalcCRC32 (0, (const u8 *)e->name, strlen (e->name));
		}
		if (!hash)
			hash = 0xABCD0000 + i;

		u16 w = 32, h = 32;
		u8 format = 3; // default I8
		const u8 *payload = e->data;
		u32 payload_sz = e->size;

		if (e->data && e->size >= 0x28 && memcmp (e->data, "\x00\x20\xaf\x30", 4) == 0)
		{
			const tpl_header_t *th = (const tpl_header_t *)e->data;
			u32 n_img = be32 (&th->n_image);
			u32 img_tab_off = be32 (&th->imgtab_off);
			if (n_img > 0 && img_tab_off + 4 <= e->size)
			{
				u32 img_hdr_off = be32 (e->data + img_tab_off);
				if (img_hdr_off + sizeof (tpl_img_header_t) <= e->size)
				{
					const tpl_img_header_t *ti = (const tpl_img_header_t *)(e->data + img_hdr_off);
					h = be16 (&ti->height);
					w = be16 (&ti->width);
					u32 iform = be32 (&ti->iform);
					switch (iform)
					{
						case IMG_I4: format = 0x2; break;
						case IMG_I8: format = 0x3; break;
						case IMG_IA4: format = 0x4; break;
						case IMG_RGB5A3: format = 0x5; break;
						case IMG_CMPR: format = 0x6; break;
						case IMG_RGB565: format = 0x7; break;
						case IMG_RGBA32: format = 0x8; break;
						default: format = 0x3; break;
					}
					u32 d_off = be32 (&ti->data_off);
					if (d_off < e->size)
					{
						payload = e->data + d_off;
						payload_sz = e->size - d_off;
					}
				}
			}
		}

		u32 sect_size = th_size + payload_sz;

		// Entry in table
		u8 *ent = buf + tab_off + i * 16;
		write_be32 (ent + 0, hash);
		write_be32 (ent + 4, cur_img_off);
		write_be32 (ent + 8, sect_size);
		write_be32 (ent + 12, 0);

		// Per-texture header
		u8 *th = buf + data_base + cur_img_off;
		write_be32 (th + 0, 1); // mip count = 1
		write_be32 (th + 4, 2); // unk
		th[8] = 5;
		th[9] = format;
		th[10] = 5;
		th[11] = 0;
		if (is_gc)
		{
			write_be16 (th + 12, w);
			write_be16 (th + 14, h);
		}
		else
		{
			write_be16 (th + 14, w);
			write_be16 (th + 16, h);
		}

		// Payload
		if (payload && payload_sz > 0)
			memcpy (th + th_size, payload, payload_sz);

		cur_img_off += sect_size;
	}

	*dest = buf;
	*dest_size = total_size;
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

// ----------------------------------------------------------------------------
// Archive writers for the container formats above.
//
// Each writer emits a canonical layout: entries in sorted name order, the
// smallest header the reader accepts, and the alignment the format's own
// files use. Re-encoding an archive that one of these writers produced
// reproduces its bytes exactly; fields the readers do not surface (padding,
// vendor-private words) are written as zero.
// ----------------------------------------------------------------------------

static u32 align_up (u32 value, u32 align)
{
	return align > 1 ? (value + align - 1) & ~(align - 1) : value;
}

// Strip any directory part: these formats store leaf names only.
static ccp leaf_name (ccp name)
{
	if (!name)
		return "";
	ccp slash = strrchr (name, '/');
	return slash ? slash + 1 : name;
}

// Nintendo APAK Archive (.apak / APAK), big-endian, version 5
enumError CreateAPAKArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	const u32 file_info_size = n_entries * 64;
	const u32 data_start = align_up (24 + file_info_size, 32);

	u32 total = data_start;
	for (uint i = 0; i < n_entries; i++)
		total = align_up (total + sorted[i].size, 32);

	u8 *buf = CALLOC (total, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (buf, "APAK", 4);
	wr_be16 (buf + 6, 5);
	wr_be32 (buf + 8, n_entries);
	wr_be32 (buf + 16, file_info_size);

	u32 data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		const uint entry_pos = 24 + i * 64;
		wr_be32 (buf + entry_pos + 4, data_off);
		wr_be32 (buf + entry_pos + 8, sorted[i].size);

		ccp name = leaf_name (sorted[i].name);
		strncpy ((char *)buf + entry_pos + 32, name, 32);

		if (sorted[i].data && sorted[i].size)
			memcpy (buf + data_off, sorted[i].data, sorted[i].size);
		data_off = align_up (data_off + sorted[i].size, 32);
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = total;
	return ERR_OK;
}

// Nintendo Switch NX Archive (.nxarc / RAXN), little-endian
enumError CreateNXARCArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	// Index 0 is the string-table pseudo entry the reader skips.
	const u32 file_count = n_entries + 1;
	const u32 header_size = 32;
	const u32 offset_block = header_size + file_count * 32;

	u32 names_len = 1; // empty name for the pseudo entry
	for (uint i = 0; i < n_entries; i++)
		names_len += (u32)strlen (leaf_name (sorted[i].name)) + 1;

	const u32 data_start = align_up (offset_block + names_len, 16);
	u32 total = data_start;
	for (uint i = 0; i < n_entries; i++)
		total = align_up (total + sorted[i].size, 16);

	u8 *buf = CALLOC (total, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (buf, "RAXN", 4);
	wr_le32 (buf + 12, offset_block);
	wr_le32 (buf + 16, header_size);
	wr_le32 (buf + 20, file_count);

	u32 name_pos = offset_block + 1; // pseudo entry already wrote its terminator
	u32 data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = leaf_name (sorted[i].name);
		const size_t nlen = strlen (name);
		memcpy (buf + name_pos, name, nlen + 1);
		name_pos += (u32)nlen + 1;

		const uint entry_pos = header_size + (i + 1) * 32;
		wr_le32 (buf + entry_pos, sorted[i].size);
		wr_le32 (buf + entry_pos + 8, data_off);

		if (sorted[i].data && sorted[i].size)
			memcpy (buf + data_off, sorted[i].data, sorted[i].size);
		data_off = align_up (data_off + sorted[i].size, 16);
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = total;
	return ERR_OK;
}

// PlatinumGames Archive (.pkz), little-endian, members stored uncompressed
enumError CreatePKZArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	const u32 offset_file_info = 32;
	const u32 str_table_pos = offset_file_info + n_entries * 32;

	u32 names_len = 0;
	for (uint i = 0; i < n_entries; i++)
		names_len += (u32)strlen (leaf_name (sorted[i].name)) + 1;

	const u32 data_start = align_up (str_table_pos + names_len, 16);
	u32 total = data_start;
	for (uint i = 0; i < n_entries; i++)
		total = align_up (total + sorted[i].size, 16);

	u8 *buf = CALLOC (total, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (buf, "pkz\0", 4);
	wr_le32 (buf + 16, n_entries);
	wr_le32 (buf + 20, offset_file_info);

	u32 name_rel = 0;
	u32 data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = leaf_name (sorted[i].name);
		const size_t nlen = strlen (name);
		memcpy (buf + str_table_pos + name_rel, name, nlen + 1);

		const uint entry_pos = offset_file_info + i * 32;
		wr_le32 (buf + entry_pos, name_rel);
		wr_le32 (buf + entry_pos + 8, sorted[i].size);
		wr_le32 (buf + entry_pos + 16, data_off);
		wr_le32 (buf + entry_pos + 24, sorted[i].size); // stored size == raw size

		if (sorted[i].data && sorted[i].size)
			memcpy (buf + data_off, sorted[i].data, sorted[i].size);
		data_off = align_up (data_off + sorted[i].size, 16);
		name_rel += (u32)nlen + 1;
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = total;
	return ERR_OK;
}

// Twilight Princess HD Archive (.pack / TMPK), big-endian
enumError CreateTMPKArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	const u32 alignment = 32;
	const u32 names_start = 16 + n_entries * 16;

	u32 names_len = 0;
	for (uint i = 0; i < n_entries; i++)
		names_len += (u32)strlen (leaf_name (sorted[i].name)) + 1;

	const u32 data_start = align_up (names_start + names_len, alignment);
	u32 total = data_start;
	for (uint i = 0; i < n_entries; i++)
		total = align_up (total + sorted[i].size, alignment);

	u8 *buf = CALLOC (total, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (buf, "TMPK", 4);
	wr_be32 (buf + 4, n_entries);
	wr_be32 (buf + 8, alignment);

	u32 name_pos = names_start;
	u32 data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = leaf_name (sorted[i].name);
		const size_t nlen = strlen (name);
		memcpy (buf + name_pos, name, nlen + 1);

		const uint entry_pos = 16 + i * 16;
		wr_be32 (buf + entry_pos, name_pos);
		wr_be32 (buf + entry_pos + 4, data_off);
		wr_be32 (buf + entry_pos + 8, sorted[i].size);

		if (sorted[i].data && sorted[i].size)
			memcpy (buf + data_off, sorted[i].data, sorted[i].size);
		data_off = align_up (data_off + sorted[i].size, alignment);
		name_pos += (u32)nlen + 1;
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = total;
	return ERR_OK;
}

// Nintendo Switch Joy-Con Vibration Archive (.vibs), little-endian
enumError CreateVIBSArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	const u32 data_start = align_up (8 + n_entries * 44, 16);
	u32 total = data_start;
	for (uint i = 0; i < n_entries; i++)
		total = align_up (total + sorted[i].size, 16);

	u8 *buf = CALLOC (total, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	wr_le32 (buf, 1); // version
	wr_le32 (buf + 4, n_entries);

	u32 data_off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		const uint entry_pos = 8 + i * 44;
		strncpy ((char *)buf + entry_pos, leaf_name (sorted[i].name), 24);
		wr_le32 (buf + entry_pos + 32, sorted[i].size);
		wr_le32 (buf + entry_pos + 40, data_off);

		if (sorted[i].data && sorted[i].size)
			memcpy (buf + data_off, sorted[i].data, sorted[i].size);
		data_off = align_up (data_off + sorted[i].size, 16);
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = total;
	return ERR_OK;
}

// Pokemon Mystery Dungeon Resource Container (.sir0 / SIR0), little-endian
enumError CreateSIR0Archive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	const nintendo_sarc_entry_t *data_ent = 0;
	const nintendo_sarc_entry_t *sub_ent = 0;

	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = leaf_name (entries[i].name);
		if (!strcasecmp (name, "data.bin") || !strcasecmp (name, "data"))
			data_ent = entries + i;
		else if (!strcasecmp (name, "subheader.bin") || !strcasecmp (name, "subheader"))
			sub_ent = entries + i;
	}

	if (!data_ent && n_entries >= 1)
		data_ent = entries;
	if (!sub_ent && n_entries >= 2 && entries + 1 != data_ent)
		sub_ent = entries + 1;

	const u32 data_sz = data_ent ? data_ent->size : 0;
	const u32 sub_sz = sub_ent ? sub_ent->size : 0;

	const u32 sub_offset = 0x10 + data_sz;
	const u32 pointer_offsets = sub_offset + sub_sz;
	const u32 total = pointer_offsets + 16; // trailing padding/pointer table

	u8 *buf = CALLOC (total, 1);
	if (!buf)
		return ERR_OUT_OF_MEMORY;

	memcpy (buf, "SIR0", 4);
	wr_le32 (buf + 4, sub_offset);
	wr_le32 (buf + 8, pointer_offsets);
	wr_le32 (buf + 12, 0);

	if (data_ent && data_ent->data && data_sz)
		memcpy (buf + 0x10, data_ent->data, data_sz);
	if (sub_ent && sub_ent->data && sub_sz)
		memcpy (buf + sub_offset, sub_ent->data, sub_sz);

	*dest = buf;
	*dest_size = total;
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
	wr_le16 (buf + 8, 1);                  // file_group_count
	wr_le16 (buf + 10, (u16)n_entries);    // file_count
	wr_le32 (buf + 12, grp_off);           // file_group_offset
	wr_le32 (buf + 16, info_off);          // file_info_offset
	wr_le32 (buf + 20, data_tbl_off);      // data_offset
	memcpy (buf + 24, "queen\0\0\0", 8);   // codename

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

// Nintendo Switch MTXT texture archive (.mtxt): a four-byte magic, a flags
// word, then a gzip stream wrapping one XTX texture container.
//
// The reader also slices the XTX's own mip surfaces out into
// "surface_%04u.bin" side-products for convenience; those are copies of
// bytes already inside texture.xtx, so the writer consumes only the XTX
// itself and lets the reader regenerate them.
enumError CreateMTXTArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	const nintendo_sarc_entry_t *xtx = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = leaf_name (entries[i].name);
		if (!strcasecmp (name, "texture.xtx"))
		{
			xtx = entries + i;
			break;
		}
	}
	// Fall back to the sole member when it isn't named texture.xtx, but
	// never guess between several candidates.
	if (!xtx && n_entries == 1)
		xtx = entries;
	if (!xtx || !xtx->data || !xtx->size)
		return ERR_NOTHING_TO_DO;

	z_stream zs;
	memset (&zs, 0, sizeof (zs));
	// Pin the gzip parameters so the same XTX always deflates to the same
	// bytes: level 9, the default strategy, and a gzip wrapper.
	if (deflateInit2 (&zs, 9, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
		return ERR_CANT_CREATE;

	const uLong bound = deflateBound (&zs, xtx->size);
	u8 *buf = MALLOC (8 + bound);
	if (!buf)
	{
		deflateEnd (&zs);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (buf, "MTXT", 4);
	wr_le32 (buf + 4, 0); // flags

	zs.next_in = (Bytef *)xtx->data;
	zs.avail_in = xtx->size;
	zs.next_out = buf + 8;
	zs.avail_out = bound;

	if (deflate (&zs, Z_FINISH) != Z_STREAM_END)
	{
		deflateEnd (&zs);
		FREE (buf);
		return ERR_CANT_CREATE;
	}
	const uint out_size = 8 + (uint)zs.total_out;
	deflateEnd (&zs);

	*dest = buf;
	*dest_size = out_size;
	return ERR_OK;
}

// Mario Kart Arcade GP DX layout archive ("pack"), little-endian.
//
// Offsets in the entry table are relative to the aligned data block rather
// than to the file, and the name pool lives inside that block too. The
// canonical layout below puts the pool at the head of the block
// (str_pool_offset 0) and aligns every member to 32 bytes.
//
// .pac is already claimed by the HAL Laboratory / Game Arts container, so
// CREATE selects this format on the .mkgpdx extension, the same way
// .sarcle and .at7p disambiguate their own overloaded extensions.
enumError CreateMKGPDXPacArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	const u32 alignment = 32;
	const u32 data_block_pos = align_up (20 + n_entries * 16, alignment);

	u32 names_len = 0;
	for (uint i = 0; i < n_entries; i++)
		names_len += (u32)strlen (leaf_name (sorted[i].name)) + 1;

	// Member offsets are relative to data_block_pos, and the name pool
	// occupies the start of the block.
	const u32 first_member_rel = align_up (names_len, alignment);
	u32 total_rel = first_member_rel;
	for (uint i = 0; i < n_entries; i++)
		total_rel = align_up (total_rel + sorted[i].size, alignment);

	const u32 total = data_block_pos + total_rel;
	u8 *buf = CALLOC (total, 1);
	if (!buf)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (buf, "pack", 4);
	wr_le32 (buf + 4, n_entries);
	wr_le32 (buf + 8, 0); // string pool at the head of the data block
	wr_le32 (buf + 12, alignment);

	u32 name_rel = 0;
	u32 member_rel = first_member_rel;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = leaf_name (sorted[i].name);
		const size_t nlen = strlen (name);
		memcpy (buf + data_block_pos + name_rel, name, nlen + 1);

		const uint entry_pos = 20 + i * 16;
		wr_le32 (buf + entry_pos + 4, name_rel);
		wr_le32 (buf + entry_pos + 8, member_rel);
		wr_le32 (buf + entry_pos + 12, sorted[i].size);

		if (sorted[i].data && sorted[i].size)
			memcpy (buf + data_block_pos + member_rel, sorted[i].data, sorted[i].size);
		member_rel = align_up (member_rel + sorted[i].size, alignment);
		name_rel += (u32)nlen + 1;
	}

	FREE (sorted);
	*dest = buf;
	*dest_size = total;
	return ERR_OK;
}

// ----------------------------------------------------------------------------
// Bandai Namco NUS3AUDIO audio archive (.nus3audio / NUS3)
//
// Used by Super Smash Bros. Ultimate (and Namco's other NUS3 middleware
// titles) to bundle per-track audio streams. Little-endian throughout:
//
//   0x00  "NUS3"
//   0x04  u32 body size
//   0x08  chunks, each an 8-byte tag (4 characters, NUL-padded) followed by
//         a u32 size and that many payload bytes:
//           AUDIINDX  u32 track count
//           TNID      u32 track id per track
//           NMOF      u32 offset into TNNM per track
//           ADOF      u32 offset, u32 size into PACK per track
//           TNNM      name table: u8 length, name bytes, NUL terminator
//           PACK      the concatenated track payloads
//
// Track payloads are whole audio files. IDSP and Opus are the two that turn
// up in practice, so name members by their own magic and fall back to .bin.
// ----------------------------------------------------------------------------
enumError ExtractNUS3AudioArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".nus3audio") && !is_ext_match (arg, ".nus3bank")
		&& !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (raw_size < 16 || memcmp (raw, "NUS3", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u8 *nmof = 0, *adof = 0, *tnnm = 0;
	uint nmof_size = 0, adof_size = 0, tnnm_size = 0;
	const u8 *pack = 0;
	uint pack_size = 0;
	u32 n_tracks = 0;

	for (size_t pos = 8; pos + 12 <= raw_size;)
	{
		const u8 *tag = raw + pos;
		const u32 csize = rd_le32 (raw + pos + 8);
		const size_t payload = pos + 12;
		if (csize > raw_size - payload)
			break;

		if (!memcmp (tag, "AUDIINDX", 8) && csize >= 4)
			n_tracks = rd_le32 (raw + payload);
		else if (!memcmp (tag, "NMOF", 4))
			nmof = raw + payload, nmof_size = csize;
		else if (!memcmp (tag, "ADOF", 4))
			adof = raw + payload, adof_size = csize;
		else if (!memcmp (tag, "TNNM", 4))
			tnnm = raw + payload, tnnm_size = csize;
		else if (!memcmp (tag, "PACK", 4))
			pack = raw + payload, pack_size = csize;

		pos = payload + csize;
	}

	if (!n_tracks || n_tracks > 100000 || !adof || !pack
		|| adof_size < (u64)n_tracks * 8)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NUS3AUDIO:%s (%u tracks) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n_tracks, dest);

	for (uint i = 0; i < n_tracks; i++)
	{
		const u32 off = rd_le32 (adof + i * 8);
		const u32 size = rd_le32 (adof + i * 8 + 4);
		if (off > pack_size || size > pack_size - off)
			continue;
		const u8 *data = pack + off;

		// Names are optional: a track without one is keyed by its index.
		char name[PATH_MAX];
		name[0] = 0;
		if (tnnm && nmof && nmof_size >= (u64)(i + 1) * 4)
		{
			const u32 noff = rd_le32 (nmof + i * 4);
			if (noff < tnnm_size)
			{
				const uint nlen = tnnm[noff];
				if (nlen && noff + 1 + nlen <= tnnm_size)
				{
					memcpy (name, tnnm + noff + 1, nlen);
					name[nlen] = 0;
				}
			}
		}
		// Track names come straight out of the file, so keep them to a
		// single plain filename rather than letting one escape the
		// destination directory.
		bool name_ok = name[0] != 0;
		for (ccp c = name; name_ok && *c; c++)
			if (*c == '/' || *c == '\\' || (u8)*c < 0x20)
				name_ok = false;
		if (name_ok && (!strcmp (name, ".") || !strcmp (name, "..")))
			name_ok = false;
		if (!name_ok)
			snprintf (name, sizeof (name), "track_%04u", i);

		ccp ext = ".bin";
		if (size >= 4)
		{
			if (!memcmp (data, "IDSP", 4))
				ext = ".idsp";
			else if (!memcmp (data, "OPUS", 4) || !memcmp (data, "OpusHead", 4))
				ext = ".lopus";
			else if (!memcmp (data, "BNSF", 4))
				ext = ".bnsf";
			else if (!memcmp (data, "RIFF", 4))
				ext = ".wav";
		}

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s%s", dest, name, ext);
		if (!testmode && size)
			SaveFile (out_path, 0, 0, data, size, 0);
	}

	FREE (raw);
	return ERR_OK;
}

enumError CreateNUS3AudioArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	nintendo_sarc_entry_t *sorted = MALLOC (n_entries * sizeof (*sorted));
	if (!sorted)
		return ERR_OUT_OF_MEMORY;
	memcpy (sorted, entries, n_entries * sizeof (*sorted));
	qsort (sorted, n_entries, sizeof (*sorted), compare_archive_entries);

	// Strip directory and extension from each entry to get the track name
	char (*names)[PATH_MAX] = CALLOC (n_entries, sizeof (*names));
	if (!names)
	{
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	u32 tnnm_size = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp leaf = leaf_name (sorted[i].name);
		snprintf (names[i], sizeof (names[i]), "%s", leaf);
		char *dot = strrchr (names[i], '.');
		if (dot)
			*dot = 0;
		tnnm_size += 1 + (u32)strlen (names[i]) + 1; // 1 byte len + chars + NUL
	}

	const u32 audiindx_len = 4;
	const u32 tnid_len = n_entries * 4;
	const u32 nmof_len = n_entries * 4;
	const u32 adof_len = n_entries * 8;
	const u32 tnnm_chunk_len = tnnm_size;

	u32 pack_len = 0;
	for (uint i = 0; i < n_entries; i++)
		pack_len += sorted[i].size;

	const u32 body_size = (8 + 4 + audiindx_len)
		+ (8 + 4 + tnid_len)
		+ (8 + 4 + nmof_len)
		+ (8 + 4 + adof_len)
		+ (8 + 4 + tnnm_chunk_len)
		+ (8 + 4 + pack_len);

	const u32 total_size = 8 + body_size;
	u8 *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (names);
		FREE (sorted);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (out, "NUS3", 4);
	wr_le32 (out + 4, body_size);

	u32 pos = 8;

	// 1. AUDIINDX
	memcpy (out + pos, "AUDIINDX", 8);
	wr_le32 (out + pos + 8, audiindx_len);
	wr_le32 (out + pos + 12, n_entries);
	pos += 12 + audiindx_len;

	// 2. TNID
	memcpy (out + pos, "TNID\0\0\0\0", 8);
	wr_le32 (out + pos + 8, tnid_len);
	for (uint i = 0; i < n_entries; i++)
		wr_le32 (out + pos + 12 + i * 4, 100 + i);
	pos += 12 + tnid_len;

	// 3. NMOF
	memcpy (out + pos, "NMOF\0\0\0\0", 8);
	wr_le32 (out + pos + 8, nmof_len);
	u32 cur_tnnm_off = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		wr_le32 (out + pos + 12 + i * 4, cur_tnnm_off);
		cur_tnnm_off += 1 + (u32)strlen (names[i]) + 1;
	}
	pos += 12 + nmof_len;

	// 4. ADOF
	memcpy (out + pos, "ADOF\0\0\0\0", 8);
	wr_le32 (out + pos + 8, adof_len);
	u32 cur_pack_off = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		wr_le32 (out + pos + 12 + i * 8, cur_pack_off);
		wr_le32 (out + pos + 12 + i * 8 + 4, sorted[i].size);
		cur_pack_off += sorted[i].size;
	}
	pos += 12 + adof_len;

	// 5. TNNM
	memcpy (out + pos, "TNNM\0\0\0\0", 8);
	wr_le32 (out + pos + 8, tnnm_chunk_len);
	u32 tnnm_payload = pos + 12;
	for (uint i = 0; i < n_entries; i++)
	{
		const u8 nlen = (u8)strlen (names[i]);
		out[tnnm_payload++] = nlen;
		memcpy (out + tnnm_payload, names[i], nlen);
		tnnm_payload += nlen;
		out[tnnm_payload++] = 0; // NUL terminator
	}
	pos += 12 + tnnm_chunk_len;

	// 6. PACK
	memcpy (out + pos, "PACK\0\0\0\0", 8);
	wr_le32 (out + pos + 8, pack_len);
	u32 pack_payload = pos + 12;
	for (uint i = 0; i < n_entries; i++)
	{
		if (sorted[i].data && sorted[i].size)
			memcpy (out + pack_payload, sorted[i].data, sorted[i].size);
		pack_payload += sorted[i].size;
	}

	FREE (names);
	FREE (sorted);

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}

