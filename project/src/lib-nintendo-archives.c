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
	if (!count || 4 + count * 4 > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Validate first offset
	const u32 first_off = rd_be32 (raw + 4);
	if (first_off < 4 + count * 4 || first_off + 16 > raw_size)
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
