// SPDX-License-Identifier: GPL-2.0+
#include "lib-lzbin.h"
#include "lib-lz10.h"
#include "lib-archive-util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool IsLZBIN (const u8 *data, uint size)
{
	if (!data || size < 16)
		return false;

	const u32 num_files = rd_le32 (data);
	if (num_files < 1 || num_files > 4000)
		return false;

	const u64 tab_size = 4 + (u64)num_files * 8;
	if (tab_size > size)
		return false;

	const u32 first_off = rd_le32 (data + 4);
	const u32 first_sz = rd_le32 (data + 8);

	if (first_off < (num_files * 8) || first_off + 4 >= size)
		return false;
	if (first_off + 4 + (u64)first_sz > size)
		return false;

	const u8 magic = data[first_off + 4];
	if (magic != 0x10 && magic != 0x11)
		return false;

	return true;
}

enumError ScanLZBIN (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || !IsLZBIN (data, size))
		return ERR_INVALID_DATA;

	const u32 num_files = rd_le32 (data);
	*entries = 0;
	*n_entries = 0;

	nintendo_sarc_entry_t *out = CALLOC (num_files, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint out_cnt = 0;
	for (u32 i = 0; i < num_files; i++)
	{
		const u32 off = rd_le32 (data + 4 + i * 8);
		const u32 csize = rd_le32 (data + 4 + i * 8 + 4);

		if (off + 4 + csize > size)
			continue;

		u8 *uncomp = 0;
		uint uncomp_size = 0;
		enumError err = DecodeLZ10LZ11 (&uncomp, &uncomp_size, data + off + 4, csize);
		if (err || !uncomp)
		{
			// Fallback: store raw compressed bytes
			char name[64];
			snprintf (name, sizeof (name), "file%03u.lz", i);
			OwnedEntryAdd (out, out_cnt++, name, data + off + 4, csize);
			continue;
		}

		ccp ext = "dat";
		if (uncomp_size >= 4 && (!memcmp (uncomp, "HBDF", 4) || !memcmp (uncomp, "HSDF", 4)))
			ext = "hbdf";

		char name[64];
		snprintf (name, sizeof (name), "file%03u.%s", i, ext);
		OwnedEntryAdd (out, out_cnt++, name, uncomp, uncomp_size);
		FREE (uncomp);
	}

	*entries = out;
	*n_entries = out_cnt;
	return ERR_OK;
}

enumError CreateLZBIN (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	u8 **comp_data = CALLOC (n_entries, sizeof (u8 *));
	uint *comp_sizes = CALLOC (n_entries, sizeof (uint));
	if (!comp_data || !comp_sizes)
	{
		FREE (comp_data);
		FREE (comp_sizes);
		return ERR_CANT_CREATE;
	}

	for (uint i = 0; i < n_entries; i++)
	{
		u8 *cd = 0;
		uint cs = 0;
		EncodeLZ10LZ11 (&cd, &cs, entries[i].data, entries[i].size, false);
		comp_data[i] = cd;
		comp_sizes[i] = cs;
	}

	uint tab_size = 4 + n_entries * 8;
	uint cur_offset = tab_size;

	// Total size calculation
	uint total_size = tab_size;
	for (uint i = 0; i < n_entries; i++)
		total_size += 4 + comp_sizes[i];

	u8 *out = CALLOC (1, total_size);
	if (!out)
	{
		for (uint i = 0; i < n_entries; i++)
			FREE (comp_data[i]);
		FREE (comp_data);
		FREE (comp_sizes);
		return ERR_CANT_CREATE;
	}

	wr_le32 (out, n_entries);
	for (uint i = 0; i < n_entries; i++)
	{
		wr_le32 (out + 4 + i * 8, cur_offset);
		wr_le32 (out + 4 + i * 8 + 4, comp_sizes[i]);

		// File at cur_offset + 4
		memcpy (out + cur_offset + 4, comp_data[i], comp_sizes[i]);
		cur_offset += 4 + comp_sizes[i];
	}

	for (uint i = 0; i < n_entries; i++)
		FREE (comp_data[i]);
	FREE (comp_data);
	FREE (comp_sizes);

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}

bool looks_like_lzbin_dir (ccp dir)
{
	if (!dir || !*dir)
		return false;
	const size_t len = strlen (dir);
	if (len > 8 && !strcasecmp (dir + len - 8, ".lzbin.d"))
		return true;
	return false;
}

enumError create_lzbin_dir (ccp source, ccp dest)
{
	sarc_build_list_t list;
	memset (&list, 0, sizeof (list));
	enumError err = collect_sarc_dir (&list, source, "");
	if (err)
	{
		reset_sarc_build_list (&list);
		return err;
	}

	u8 *bin = 0;
	uint bin_size = 0;
	err = CreateLZBIN (&bin, &bin_size, list.entry, list.used);
	reset_sarc_build_list (&list);
	if (err || !bin)
	{
		FREE (bin);
		return err ? err : ERR_CANT_CREATE;
	}

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, 0);
	if (!err && F.f)
	{
		if (fwrite (bin, 1, bin_size, F.f) != bin_size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing LZBIN failed: %s\n", dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (bin);
	return err;
}
