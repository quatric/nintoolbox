#include "lib-std.h"
#include "lib-narc.h"
#include <string.h>
#include <errno.h>

void ResetNARC (narc_t *narc)
{
	if (narc)
	{
		if (narc->entries)
		{
			for (uint i = 0; i < narc->n_entries; i++)
				FREE (narc->entries[i].name);
			FREE (narc->entries);
		}
		memset (narc, 0, sizeof (*narc));
	}
}

enumError ScanNARC (narc_t *narc, const u8 *data, size_t size)
{
	if (!narc || !data || size < 16)
		return ERR_INVALID_DATA;
	memset (narc, 0, sizeof (*narc));

	if (memcmp (data, "NARC", 4) && memcmp (data, "CRAN", 4))
		return ERR_INVALID_DATA;

	u16 bom = rd_le16 (data + 4);
	bool is_le = (bom == 0xFFFE || !memcmp (data, "CRAN", 4));
	narc->raw = data;
	narc->raw_size = size;
	narc->is_le = is_le;

	u32 off = 16;
	const u8 *fatb_data = 0;
	uint fatb_files = 0;
	const u8 *btnf_data = 0;
	uint btnf_size = 0;
	const u8 *fimg_data = 0;
	uint fimg_size = 0;

	while (off + 8 <= size)
	{
		char ch_magic[5] = { 0 };
		memcpy (ch_magic, data + off, 4);
		u32 ch_size = is_le ? rd_le32 (data + off + 4) : rd_be32 (data + off + 4);
		if (ch_size < 8 || off + ch_size > size)
			break;

		if (!memcmp (ch_magic, "BTAF", 4) || !memcmp (ch_magic, "FATB", 4))
		{
			fatb_data = data + off;
			fatb_files = (is_le ? rd_le32 (data + off + 8) : rd_be32 (data + off + 8)) & 0xFFFF;
		}
		else if (!memcmp (ch_magic, "BTNF", 4) || !memcmp (ch_magic, "FNTB", 4))
		{
			btnf_data = data + off;
			btnf_size = ch_size;
		}
		else if (!memcmp (ch_magic, "GMIF", 4) || !memcmp (ch_magic, "FIMG", 4))
		{
			fimg_data = data + off + 8;
			fimg_size = ch_size - 8;
		}
		off += ch_size;
	}

	if (!fatb_data || !fimg_data || !fatb_files)
		return ERR_INVALID_DATA;

	narc->n_entries = fatb_files;
	narc->entries = CALLOC (fatb_files, sizeof (narc_entry_t));
	narc->fimg_data = fimg_data;
	narc->fimg_size = fimg_size;

	for (uint i = 0; i < fatb_files; i++)
	{
		const u8 *entry_ptr = fatb_data + 12 + 8 * i;
		if (entry_ptr + 8 > fatb_data + (is_le ? rd_le32 (fatb_data + 4) : rd_be32 (fatb_data + 4)))
			break;
		u32 st = is_le ? rd_le32 (entry_ptr) : rd_be32 (entry_ptr);
		u32 en = is_le ? rd_le32 (entry_ptr + 4) : rd_be32 (entry_ptr + 4);
		narc->entries[i].offset = st;
		narc->entries[i].size = en >= st ? en - st : 0;
	}

	if (btnf_data && btnf_size >= 16)
	{
		uint num_dirs = (is_le ? rd_le16 (btnf_data + 14) : rd_be16 (btnf_data + 14)) & 0x0FFF;
		if (num_dirs > 0 && num_dirs < 4096)
		{
			typedef struct narc_dir_t
			{
				u32 sub;
				u16 first;
				u16 parent;
			} narc_dir_t;
			narc_dir_t *dirs = CALLOC (num_dirs, sizeof (narc_dir_t));
			char **dir_paths = CALLOC (num_dirs, sizeof (char *));

			for (uint d = 0; d < num_dirs; d++)
			{
				if (8 + 8 * d + 8 <= btnf_size)
				{
					dirs[d].sub
						= is_le ? rd_le32 (btnf_data + 8 + 8 * d) : rd_be32 (btnf_data + 8 + 8 * d);
					dirs[d].first = is_le ? rd_le16 (btnf_data + 8 + 8 * d + 4)
										  : rd_be16 (btnf_data + 8 + 8 * d + 4);
					dirs[d].parent = (is_le ? rd_le16 (btnf_data + 8 + 8 * d + 6)
											: rd_be16 (btnf_data + 8 + 8 * d + 6))
						& 0x0FFF;
				}
			}

			for (uint d = 0; d < num_dirs; d++)
			{
				const char *parent_path = dir_paths[d] ? dir_paths[d] : "";
				uint cur_file = dirs[d].first;
				u32 pos = 8 + dirs[d].sub;

				while (pos < btnf_size)
				{
					u8 len_byte = btnf_data[pos++];
					if (len_byte == 0)
						break;

					if (len_byte & 0x80)
					{
						uint name_len = len_byte & 0x7F;
						if (pos + name_len + 2 > btnf_size)
							break;
						char dname[PATH_MAX];
						snprintf (dname, sizeof (dname), "%.*s", (int)name_len, btnf_data + pos);
						pos += name_len;
						u16 subdir_id
							= (is_le ? rd_le16 (btnf_data + pos) : rd_be16 (btnf_data + pos))
							& 0x0FFF;
						pos += 2;

						if (subdir_id < num_dirs && !dir_paths[subdir_id])
						{
							char full_d[PATH_MAX];
							if (*parent_path)
								snprintf (full_d, sizeof (full_d), "%s/%s", parent_path, dname);
							else
								snprintf (full_d, sizeof (full_d), "%s", dname);
							dir_paths[subdir_id] = STRDUP (full_d);
						}
					}
					else
					{
						uint name_len = len_byte;
						if (pos + name_len > btnf_size)
							break;
						char fname[PATH_MAX];
						snprintf (fname, sizeof (fname), "%.*s", (int)name_len, btnf_data + pos);
						pos += name_len;

						if (cur_file < narc->n_entries && !narc->entries[cur_file].name)
						{
							char full_f[PATH_MAX];
							if (*parent_path)
								snprintf (full_f, sizeof (full_f), "%s/%s", parent_path, fname);
							else
								snprintf (full_f, sizeof (full_f), "%s", fname);
							narc->entries[cur_file].name = STRDUP (full_f);
						}
						cur_file++;
					}
				}
			}

			for (uint d = 0; d < num_dirs; d++)
				FREE (dir_paths[d]);
			FREE (dir_paths);
			FREE (dirs);
		}
	}

	return ERR_OK;
}

enumError CreateNARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries, bool is_le)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xFFFF)
		return EINVAL;

	void (*w16) (u8 *, u16) = is_le ? wr_le16 : wr_be16;
	void (*w32) (u8 *, u32) = is_le ? wr_le32 : wr_be32;

	const uint btaf_header_size = 12;
	const uint btaf_entries_size = 8 * n_entries;
	const uint btaf_size = btaf_header_size + btaf_entries_size;

	typedef struct narc_build_dir_t
	{
		char *name;
		uint parent;
		uint first_file;
		uint num_files;
		uint *file_indices;
		uint num_subdirs;
		uint *subdir_indices;
	} narc_build_dir_t;

	narc_build_dir_t dirs[512];
	uint num_dirs = 1;
	memset (dirs, 0, sizeof (dirs));
	dirs[0].name = STRDUP ("");
	dirs[0].parent = 0;

	for (uint i = 0; i < n_entries; i++)
	{
		ccp full_name = entries[i].name ? entries[i].name : "file";
		char dir_part[PATH_MAX] = { 0 };
		char file_part[PATH_MAX] = { 0 };

		ccp slash = strrchr (full_name, '/');
		if (slash)
		{
			size_t dlen = slash - full_name;
			if (dlen >= sizeof (dir_part))
				dlen = sizeof (dir_part) - 1;
			memcpy (dir_part, full_name, dlen);
			dir_part[dlen] = 0;
			snprintf (file_part, sizeof (file_part), "%s", slash + 1);
		}
		else
		{
			snprintf (file_part, sizeof (file_part), "%s", full_name);
		}

		uint cur_dir = 0;
		if (dir_part[0])
		{
			char *p = dir_part;
			while (*p)
			{
				char seg[PATH_MAX];
				char *slash2 = strchr (p, '/');
				if (slash2)
				{
					size_t slen = slash2 - p;
					if (slen >= sizeof (seg))
						slen = sizeof (seg) - 1;
					memcpy (seg, p, slen);
					seg[slen] = 0;
					p = slash2 + 1;
				}
				else
				{
					snprintf (seg, sizeof (seg), "%s", p);
					p += strlen (p);
				}

				int found = -1;
				for (uint s = 0; s < dirs[cur_dir].num_subdirs; s++)
				{
					uint sidx = dirs[cur_dir].subdir_indices[s];
					if (!strcmp (dirs[sidx].name, seg))
					{
						found = (int)sidx;
						break;
					}
				}

				if (found < 0)
				{
					if (num_dirs >= 512)
						break;
					uint new_d = num_dirs++;
					dirs[new_d].name = STRDUP (seg);
					dirs[new_d].parent = cur_dir;
					dirs[cur_dir].subdir_indices = REALLOC (dirs[cur_dir].subdir_indices,
						(dirs[cur_dir].num_subdirs + 1) * sizeof (uint));
					dirs[cur_dir].subdir_indices[dirs[cur_dir].num_subdirs++] = new_d;
					cur_dir = new_d;
				}
				else
				{
					cur_dir = (uint)found;
				}
			}
		}

		dirs[cur_dir].file_indices
			= REALLOC (dirs[cur_dir].file_indices, (dirs[cur_dir].num_files + 1) * sizeof (uint));
		dirs[cur_dir].file_indices[dirs[cur_dir].num_files++] = i;
	}

	uint file_counter = 0;
	uint *file_order = CALLOC (n_entries, sizeof (uint));
	for (uint d = 0; d < num_dirs; d++)
	{
		dirs[d].first_file = file_counter;
		for (uint f = 0; f < dirs[d].num_files; f++)
			file_order[file_counter++] = dirs[d].file_indices[f];
	}

	dirs[0].parent = num_dirs;

	uint name_entries_cap = 4096;
	u8 *name_entries_buf = MALLOC (name_entries_cap);
	uint name_entries_len = 0;
	uint *dir_sub_offsets = CALLOC (num_dirs, sizeof (uint));

	for (uint d = 0; d < num_dirs; d++)
	{
		dir_sub_offsets[d] = 8 * num_dirs + name_entries_len;

		for (uint s = 0; s < dirs[d].num_subdirs; s++)
		{
			uint sidx = dirs[d].subdir_indices[s];
			ccp sname = dirs[sidx].name;
			size_t snlen = strlen (sname);
			if (snlen > 127)
				snlen = 127;

			while (name_entries_len + 1 + snlen + 2 + 1 > name_entries_cap)
			{
				name_entries_cap *= 2;
				name_entries_buf = REALLOC (name_entries_buf, name_entries_cap);
			}

			name_entries_buf[name_entries_len++] = (u8)(0x80 | snlen);
			memcpy (name_entries_buf + name_entries_len, sname, snlen);
			name_entries_len += snlen;
			w16 (name_entries_buf + name_entries_len, (u16)(0xF000 | sidx));
			name_entries_len += 2;
		}

		for (uint f = 0; f < dirs[d].num_files; f++)
		{
			uint f_orig_idx = dirs[d].file_indices[f];
			ccp full_f = entries[f_orig_idx].name ? entries[f_orig_idx].name : "file";
			ccp slash = strrchr (full_f, '/');
			ccp fname = slash ? slash + 1 : full_f;
			size_t fnlen = strlen (fname);
			if (fnlen > 127)
				fnlen = 127;

			while (name_entries_len + 1 + fnlen + 1 > name_entries_cap)
			{
				name_entries_cap *= 2;
				name_entries_buf = REALLOC (name_entries_buf, name_entries_cap);
			}

			name_entries_buf[name_entries_len++] = (u8)fnlen;
			memcpy (name_entries_buf + name_entries_len, fname, fnlen);
			name_entries_len += fnlen;
		}

		name_entries_buf[name_entries_len++] = 0;
	}

	uint btnf_raw_size = 8 + 8 * num_dirs + name_entries_len;
	uint btnf_size = (btnf_raw_size + 3) & ~3u;

	uint *file_gmif_offsets = CALLOC (n_entries, sizeof (uint));
	uint gmif_cur_offset = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		uint orig_idx = file_order[i];
		file_gmif_offsets[i] = gmif_cur_offset;
		uint sz = entries[orig_idx].size;
		gmif_cur_offset += (sz + 3) & ~3u;
	}

	uint gmif_size = 8 + gmif_cur_offset;
	uint total_narc_size = 16 + btaf_size + btnf_size + gmif_size;

	u8 *out = CALLOC (1, total_narc_size);
	if (!out)
	{
		FREE (file_order);
		FREE (file_gmif_offsets);
		FREE (dir_sub_offsets);
		FREE (name_entries_buf);
		for (uint d = 0; d < num_dirs; d++)
		{
			FREE (dirs[d].name);
			FREE (dirs[d].subdir_indices);
			FREE (dirs[d].file_indices);
		}
		return ERR_CANT_CREATE;
	}

	memcpy (out, "NARC", 4);
	w16 (out + 4, is_le ? 0xFFFE : 0xFEFF);
	w16 (out + 6, 0x0100);
	w32 (out + 8, total_narc_size);
	w16 (out + 12, 16);
	w16 (out + 14, 3);

	u8 *btaf = out + 16;
	memcpy (btaf, "BTAF", 4);
	w32 (btaf + 4, btaf_size);
	w16 (btaf + 8, n_entries);
	w16 (btaf + 10, 0);

	for (uint i = 0; i < n_entries; i++)
	{
		uint orig_idx = file_order[i];
		uint st = file_gmif_offsets[i];
		uint en = st + entries[orig_idx].size;
		w32 (btaf + 12 + 8 * i, st);
		w32 (btaf + 12 + 8 * i + 4, en);
	}

	u8 *btnf = out + 16 + btaf_size;
	memcpy (btnf, "BTNF", 4);
	w32 (btnf + 4, btnf_size);

	for (uint d = 0; d < num_dirs; d++)
	{
		w32 (btnf + 8 + 8 * d, dir_sub_offsets[d]);
		w16 (btnf + 8 + 8 * d + 4, (u16)dirs[d].first_file);
		w16 (btnf + 8 + 8 * d + 6, (u16)(d == 0 ? dirs[0].parent : (0xF000 | dirs[d].parent)));
	}

	memcpy (btnf + 8 + 8 * num_dirs, name_entries_buf, name_entries_len);

	u8 *gmif = out + 16 + btaf_size + btnf_size;
	memcpy (gmif, "GMIF", 4);
	w32 (gmif + 4, gmif_size);

	for (uint i = 0; i < n_entries; i++)
	{
		uint orig_idx = file_order[i];
		if (entries[orig_idx].size && entries[orig_idx].data)
			memcpy (
				gmif + 8 + file_gmif_offsets[i], entries[orig_idx].data, entries[orig_idx].size);
	}

	FREE (file_order);
	FREE (file_gmif_offsets);
	FREE (dir_sub_offsets);
	FREE (name_entries_buf);
	for (uint d = 0; d < num_dirs; d++)
	{
		FREE (dirs[d].name);
		FREE (dirs[d].subdir_indices);
		FREE (dirs[d].file_indices);
	}

	*dest = out;
	*dest_size = total_narc_size;
	return ERR_OK;
}
