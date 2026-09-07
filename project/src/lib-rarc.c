
/***************************************************************************
 *                         _______ _______ _______                         *
 *                        |  ___  |____   |  ___  |                        *
 *                        | |   |_|    / /| |   |_|                        *
 *                        | |_____    / / | |_____                         *
 *                        |_____  |  / /  |_____  |                        *
 *                         _    | | / /    _    | |                        *
 *                        | |___| |/ /____| |___| |                        *
 *                        |_______|_______|_______|                        *
 *                                                                         *
 *                            Wiimms SZS Tools                             *
 *                          https://szs.wiimm.de/                          *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This file is part of the SZS project.                                 *
 *   Visit https://szs.wiimm.de/ for project details and sources.          *
 *                                                                         *
 *   Copyright (c) 2011-2024 by Dirk Clemens <wiimm@wiimm.de>              *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   See file gpl-2.0.txt or http://www.gnu.org/licenses/gpl-2.0.txt       *
 *                                                                         *
 ***************************************************************************/

#include "lib-rarc.h"
#include "lib-nintendo.h"

///////////////////////////////////////////////////////////////////////////////

// Returns the byte length (1-4) of a valid UTF-8 sequence starting at 'p',
// or 0 if 'p' does not start one. See the matching helper/comment in
// lib-szs.c's IterateFilesU8() -- same real-disc problem (Twilight
// Princess's RARC member names carry raw non-UTF-8 bytes), different
// container format's own filename-copy site.
static uint ValidUTF8SeqLenRARC (const u8 *p)
{
	if (p[0] < 0x80)
		return 1;
	if ((p[0] & 0xe0) == 0xc0)
		return (p[1] & 0xc0) == 0x80 ? 2 : 0;
	if ((p[0] & 0xf0) == 0xe0)
		return (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 ? 3 : 0;
	if ((p[0] & 0xf8) == 0xf0)
		return (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 && (p[3] & 0xc0) == 0x80 ? 4 : 0;
	return 0;
}

static char *StringCopySanitizedUTF8E (char *buf, ccp buf_end, ccp src)
{
	const u8 *s = (const u8 *)src;
	char *dest = buf;
	while (dest < buf_end && *s)
	{
		const uint seqlen = ValidUTF8SeqLenRARC (s);
		if (!seqlen)
		{
			s++;
			*dest++ = '_';
			continue;
		}
		uint i;
		for (i = 0; i < seqlen && dest < buf_end; i++, s++)
			*dest++ = (char)*s;
	}
	if (dest < buf_end)
		*dest = 0;
	return dest;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			IterateFilesRARC()		///////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct rarc_info_t
{
	const u8 *data; // begin of file data
	uint data_off; // offset of 'data'

	const rarc_header_t *rh; // RARC header
	const rarc_node_t *node; // first RARC node
	const rarc_entry_t *entry; // first RARC entry
	ccp str_pool; // pointer to string pool
	ccp str_pool_end; // pointer to string pool end
	uint n_node; // total number of nodes
	uint n_entry; // total number of entires

} rarc_info_t;

///////////////////////////////////////////////////////////////////////////////

// Maximum supported RARC directory depth (mirrors the U8 iterator's MAX_DEPTH
// in lib-szs.c). Guards iterate_rarc_dir() against stack overflow on corrupt /
// cyclic directory graphs where a subdirectory entry points back to a node
// already on the recursion path (e.g. SMG's ChaosLandZoneMap.arc, which
// repeats 'camera/CameraParam.bcam/' >25 levels deep).
#define RARC_MAX_DIR_DEPTH 25

static int iterate_rarc_dir (szs_iterator_t *it, // iterator struct with all infos
	rarc_info_t *ri, // valid rarc info
	uint node_idx // node to print
)
{
	DASSERT (it);
	DASSERT (it->szs);
	DASSERT (ri);
	DASSERT (ri->rh);
	DASSERT (ri->node);
	DASSERT (ri->entry);

	if (node_idx >= ri->n_node)
		return 0;

	if (it->depth >= RARC_MAX_DIR_DEPTH)
		return 0;

	const rarc_node_t *node = ri->node + node_idx;
	uint idx = be32 (&node->entry_index);
	if (idx >= ri->n_entry)
		return 0;
	uint n = be16 (&node->n_entry);
	if (n > ri->n_entry - idx)
		n = ri->n_entry - idx;

	char *path = it->trail_path;
	char *path_end = it->path + sizeof (it->path) - 4;
	const rarc_entry_t *entry = ri->entry + idx;

	int stat;
	for (stat = 0; !stat && n > 0; idx++, n--, entry++)
	{
		it->index = idx;

		ccp fname = ri->str_pool + be16 (&entry->name_off);
		if (*fname == '.' && (!fname[1] || (fname[1] == '.' && !fname[2])))
			continue;

		char *dest = StringCopySanitizedUTF8E (path, path_end, fname);
		if (be16 (&entry->id) != 0xffff)
		{
			// is file

			it->is_dir = 0;
			it->off = ri->data_off + be32 (&entry->data_off);
			it->size = be32 (&entry->data_size);
			stat = it->func_it (it, false);
		}
		else
		{
			// is directory

			*dest++ = '/';
			*dest = 0;
			it->is_dir = 1;
			it->off = 0;
			it->size = 0;
			stat = it->func_it (it, false);

			char *save_trail_path = it->trail_path;
			it->trail_path = dest;
			it->depth++;
			iterate_rarc_dir (it, ri, be32 (&entry->data_off));
			it->depth--;
			it->trail_path = save_trail_path;
		}
	}

	return stat;
}

///////////////////////////////////////////////////////////////////////////////

int IterateFilesRARC (struct szs_iterator_t *it, // iterator struct with all infos
	bool term // true: termination hint
)
{
	if (term)
		return 0;

	DASSERT (it);
	DASSERT (it->func_it);
	szs_file_t *szs = it->szs;
	DASSERT (szs);

	if (!szs->data || szs->size <= sizeof (rarc_file_header_t))
		return -1;

	//--- analyze file header

	const rarc_file_header_t *fh = (rarc_file_header_t *)szs->data;
	const uint file_size = be32 (&fh->file_size);
	const uint hd_off = be32 (&fh->header_off);
	const uint hd_size = be32 (&fh->header_size);
	TRACE ("RARC/FH: fsize=%x, header=%x+%x\n", file_size, hd_off, hd_size);

	const u8 *data_beg = szs->data + hd_off + hd_size;
	const u8 *data_end = szs->data + szs->size;

	if (be32 (&fh->magic) != RARC_MAGIC_NUM || file_size > szs->size || hd_off >= szs->size
		|| hd_size >= szs->size || hd_off + hd_size >= szs->size)
		return -1;

	//--- analyze rarc header

	rarc_info_t ri;
	memset (&ri, 0, sizeof (ri));
	ri.data = data_beg;
	ri.data_off = ri.data - szs->data;

	ri.rh = (rarc_header_t *)(szs->data + hd_off);
	ri.node = (rarc_node_t *)((u8 *)ri.rh + be32 (&ri.rh->root_off));
	ri.entry = (rarc_entry_t *)((u8 *)ri.rh + be32 (&ri.rh->entry_off));
	ri.str_pool = (ccp)ri.rh + be32 (&ri.rh->str_pool_off);
	ri.str_pool_end = ri.str_pool + be32 (&ri.rh->str_pool_size);
	ri.n_node = be32 (&ri.rh->n_node);
	ri.n_entry = be32 (&ri.rh->n_entry);

	if ((u8 *)ri.node < (u8 *)ri.rh + sizeof (*ri.rh)
		|| (u8 *)ri.node + sizeof (*ri.node) > data_end || (u8 *)ri.entry <= (u8 *)ri.node
		|| (u8 *)ri.entry + sizeof (*ri.node) * ri.n_entry > data_end)
		return -1;

#if HAVE_PRINT0
	{
		PRINT (">> %6zx header\n", (u8 *)ri.rh - szs->data);
		PRINT (">> %6zx %u nodes\n", (u8 *)ri.node - szs->data, ri.n_node);
		PRINT (">> %6zx %u entries\n", (u8 *)ri.entry - szs->data, ri.n_entry);
		PRINT (">> %6zx string pool\n", (u8 *)ri.str_pool - szs->data);
		PRINT (">> %6zx END string pool\n", (u8 *)ri.str_pool_end - szs->data);
		PRINT (">> %6zx data\n", (u8 *)data_beg - szs->data);
		PRINT (">> %6zx END data\n", (u8 *)data_end - szs->data);
		PRINT (">> %6zx END file\n", szs->size);

		int i;

		putchar ('\n');
		const rarc_node_t *node = ri.node;
		for (i = 0; i < ri.n_node; i++, node++)
		{
			printf ("N %3d: %.4s %04x : %4u %4u : %s\n", i, node->name, be16 (&node->unknown_08),
				be16 (&node->n_entry), be32 (&node->entry_index),
				ri.str_pool + be16 (&node->name_off));
		}

		putchar ('\n');
		const rarc_entry_t *entry = ri.entry;
		for (i = 0; i < ri.n_entry; i++, entry++)
		{
			printf ("E %3d: %04x %04x %04x : %8x %6x : %08x : %s\n", i, be16 (&entry->id),
				be16 (&entry->unknown_02), be16 (&entry->unknown_04), be32 (&entry->data_off),
				be32 (&entry->data_size), be32 (&entry->unknown_10),
				ri.str_pool + be16 (&entry->name_off));
		}
	}
#endif

	if (it->itpar.cut_files)
	{
		it->index = 0;
		it->fst_item = 0;
		it->is_dir = 0;

		it->off = 0;
		it->size = sizeof (rarc_file_header_t);
		StringCopyS (it->path, sizeof (it->path), ".RARC.file-header");
		it->func_it (it, false);

		it->off = (u8 *)ri.rh - szs->data;
		it->size = sizeof (rarc_header_t);
		StringCopyS (it->path, sizeof (it->path), ".RARC.header");
		it->func_it (it, false);

		it->off = (u8 *)ri.node - szs->data;
		it->size = sizeof (rarc_node_t) * ri.n_node;
		snprintf (
			it->path, sizeof (it->path), ".RARC.%u_node%s", ri.n_node, ri.n_node == 1 ? "" : "s");
		it->func_it (it, false);

		it->off = (u8 *)ri.entry - szs->data;
		it->size = sizeof (rarc_entry_t) * ri.n_entry;
		snprintf (it->path, sizeof (it->path), ".RARC.%u_entr%s", ri.n_entry,
			ri.n_entry == 1 ? "y" : "ies");
		it->func_it (it, false);

		it->off = (u8 *)ri.str_pool - szs->data;
		it->size = ri.str_pool_end - ri.str_pool;
		StringCopyS (it->path, sizeof (it->path), ".RARC.string-pool");
		it->func_it (it, false);
	}

	it->trail_path = it->path;
	it->depth = 0;
	iterate_rarc_dir (it, &ri, 0);
	it->trail_path = 0;

	return 0;
}

typedef struct
{
	char *name;
	bool is_dir;
	uint parent_node_idx;
	uint *child_indices;
	uint num_children;
	uint data_idx;
	uint node_idx;
} rarc_build_node_t;

static u16 CalcRARCHash (ccp s)
{
	u16 h = 0;
	while (*s)
		h = (u16)(h * 3 + (u8)*s++);
	return h;
}

enumError CreateRARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xFFFF)
		return EINVAL;

	rarc_build_node_t nodes[1024];
	uint num_nodes = 1;
	memset (nodes, 0, sizeof (nodes));
	nodes[0].name = STRDUP ("ROOT");
	nodes[0].is_dir = true;
	nodes[0].parent_node_idx = 0;
	nodes[0].node_idx = 0;

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
				for (uint c = 0; c < nodes[cur_dir].num_children; c++)
				{
					uint cidx = nodes[cur_dir].child_indices[c];
					if (nodes[cidx].is_dir && !strcmp (nodes[cidx].name, seg))
					{
						found = (int)cidx;
						break;
					}
				}

				if (found < 0)
				{
					if (num_nodes >= 1024)
						break;
					uint new_d = num_nodes++;
					nodes[new_d].name = STRDUP (seg);
					nodes[new_d].is_dir = true;
					nodes[new_d].parent_node_idx = cur_dir;
					nodes[cur_dir].child_indices = REALLOC (nodes[cur_dir].child_indices,
						(nodes[cur_dir].num_children + 1) * sizeof (uint));
					nodes[cur_dir].child_indices[nodes[cur_dir].num_children++] = new_d;
					cur_dir = new_d;
				}
				else
				{
					cur_dir = (uint)found;
				}
			}
		}

		if (num_nodes < 1024)
		{
			uint new_f = num_nodes++;
			nodes[new_f].name = STRDUP (file_part);
			nodes[new_f].is_dir = false;
			nodes[new_f].parent_node_idx = cur_dir;
			nodes[new_f].data_idx = i;
			nodes[cur_dir].child_indices = REALLOC (
				nodes[cur_dir].child_indices, (nodes[cur_dir].num_children + 1) * sizeof (uint));
			nodes[cur_dir].child_indices[nodes[cur_dir].num_children++] = new_f;
		}
	}

	uint dir_nodes[1024];
	uint num_dirs = 0;
	for (uint i = 0; i < num_nodes; i++)
	{
		if (nodes[i].is_dir)
		{
			nodes[i].node_idx = num_dirs;
			dir_nodes[num_dirs++] = i;
		}
	}

	u8 *str_pool = MALLOC (65536);
	uint str_pool_cap = 65536;
	uint str_pool_len = 0;

#define APPEND_STR(s, out_off)                                                                     \
	do                                                                                             \
	{                                                                                              \
		size_t _slen = strlen (s) + 1;                                                             \
		if (str_pool_len + _slen > str_pool_cap)                                                   \
		{                                                                                          \
			str_pool_cap = (str_pool_cap + (uint)_slen) * 2;                                       \
			str_pool = REALLOC (str_pool, str_pool_cap);                                           \
		}                                                                                          \
		out_off = str_pool_len;                                                                    \
		memcpy (str_pool + str_pool_len, s, _slen);                                                \
		str_pool_len += (uint)_slen;                                                               \
	} while (0)

	uint dot_off, dotdot_off;
	APPEND_STR (".", dot_off);
	APPEND_STR ("..", dotdot_off);

	uint total_file_data = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		total_file_data = (total_file_data + 31) & ~31u;
		total_file_data += entries[i].size;
	}
	u8 *file_data_buf = CALLOC (1, total_file_data ? total_file_data : 32);

	uint cur_file_offset = 0;
	uint *entry_data_offsets = CALLOC (n_entries, sizeof (uint));
	for (uint i = 0; i < n_entries; i++)
	{
		cur_file_offset = (cur_file_offset + 31) & ~31u;
		entry_data_offsets[i] = cur_file_offset;
		if (entries[i].data && entries[i].size)
			memcpy (file_data_buf + cur_file_offset, entries[i].data, entries[i].size);
		cur_file_offset += entries[i].size;
	}

	uint total_entries_count = 0;
	for (uint d = 0; d < num_dirs; d++)
	{
		uint d_idx = dir_nodes[d];
		total_entries_count += 2 + nodes[d_idx].num_children;
	}

	uint node_table_size = num_dirs * 0x10;
	uint entry_table_size = total_entries_count * 0x14;

	u8 *node_table = CALLOC (1, node_table_size);
	u8 *entry_table = CALLOC (1, entry_table_size);

	uint cur_entry_idx = 0;
	uint file_id_counter = 0;

	for (uint d = 0; d < num_dirs; d++)
	{
		uint d_idx = dir_nodes[d];
		rarc_build_node_t *dir = &nodes[d_idx];

		uint dir_name_off = 0;
		APPEND_STR (dir->name, dir_name_off);

		char short_name[5] = "    ";
		for (int k = 0; k < 4 && dir->name[k]; k++)
			short_name[k] = (char)toupper ((unsigned char)dir->name[k]);

		u8 *node_ptr = node_table + d * 0x10;
		memcpy (node_ptr, short_name, 4);
		wr_be32 (node_ptr + 4, dir_name_off);
		wr_be16 (node_ptr + 8, CalcRARCHash (dir->name));
		wr_be16 (node_ptr + 10, (u16)(2 + dir->num_children));
		wr_be32 (node_ptr + 12, cur_entry_idx);

		// Entry: '.'
		u8 *e_dot = entry_table + cur_entry_idx++ * 0x14;
		wr_be16 (e_dot + 0, 0xFFFF);
		wr_be16 (e_dot + 2, CalcRARCHash ("."));
		wr_be16 (e_dot + 4, 0x0200); // directory
		wr_be16 (e_dot + 6, (u16)dot_off);
		wr_be32 (e_dot + 8, d); // current dir node idx
		wr_be32 (e_dot + 12, 0x10);

		// Entry: '..'
		u8 *e_dotdot = entry_table + cur_entry_idx++ * 0x14;
		wr_be16 (e_dotdot + 0, 0xFFFF);
		wr_be16 (e_dotdot + 2, CalcRARCHash (".."));
		wr_be16 (e_dotdot + 4, 0x0200); // directory
		wr_be16 (e_dotdot + 6, (u16)dotdot_off);
		uint p_node_idx = (d == 0) ? 0xFFFFFFFF : nodes[dir->parent_node_idx].node_idx;
		wr_be32 (e_dotdot + 8, p_node_idx);
		wr_be32 (e_dotdot + 12, 0x10);

		// Entries: children (subdirs first, then files)
		for (uint c = 0; c < dir->num_children; c++)
		{
			uint c_idx = dir->child_indices[c];
			rarc_build_node_t *child = &nodes[c_idx];
			if (child->is_dir)
			{
				uint c_name_off = 0;
				APPEND_STR (child->name, c_name_off);
				u8 *e_child = entry_table + cur_entry_idx++ * 0x14;
				wr_be16 (e_child + 0, 0xFFFF);
				wr_be16 (e_child + 2, CalcRARCHash (child->name));
				wr_be16 (e_child + 4, 0x0200);
				wr_be16 (e_child + 6, (u16)c_name_off);
				wr_be32 (e_child + 8, child->node_idx);
				wr_be32 (e_child + 12, 0x10);
			}
		}
		for (uint c = 0; c < dir->num_children; c++)
		{
			uint c_idx = dir->child_indices[c];
			rarc_build_node_t *child = &nodes[c_idx];
			if (!child->is_dir)
			{
				uint c_name_off = 0;
				APPEND_STR (child->name, c_name_off);
				u8 *e_child = entry_table + cur_entry_idx++ * 0x14;
				wr_be16 (e_child + 0, (u16)file_id_counter++);
				wr_be16 (e_child + 2, CalcRARCHash (child->name));
				wr_be16 (e_child + 4, 0x1100); // file
				wr_be16 (e_child + 6, (u16)c_name_off);
				wr_be32 (e_child + 8, entry_data_offsets[child->data_idx]);
				wr_be32 (e_child + 12, entries[child->data_idx].size);
			}
		}
	}

	uint str_pool_aligned = (str_pool_len + 31) & ~31u;
	u8 *str_pool_buf = CALLOC (1, str_pool_aligned);
	memcpy (str_pool_buf, str_pool, str_pool_len);
	FREE (str_pool);

	uint header_data_size = 0x20 + node_table_size + entry_table_size + str_pool_aligned;
	header_data_size = (header_data_size + 31) & ~31u;

	uint total_rarc_size = 0x20 + header_data_size + cur_file_offset;
	u8 *out = CALLOC (1, total_rarc_size);

	// File header (32 bytes)
	memcpy (out, "RARC", 4);
	wr_be32 (out + 4, total_rarc_size);
	wr_be32 (out + 8, 0x20); // header_off
	wr_be32 (out + 12, header_data_size); // header_size

	// RARC header (32 bytes at offset 0x20)
	u8 *rh = out + 0x20;
	wr_be32 (rh + 0, num_dirs);
	wr_be32 (rh + 4, 0x20); // root_off (offset from rh)
	wr_be32 (rh + 8, total_entries_count);
	wr_be32 (rh + 12, 0x20 + node_table_size); // entry_off (offset from rh)
	wr_be32 (rh + 16, str_pool_aligned);
	wr_be32 (rh + 20, 0x20 + node_table_size + entry_table_size); // str_pool_off
	wr_be16 (rh + 24, (u16)total_entries_count);

	// Node table
	memcpy (rh + 0x20, node_table, node_table_size);
	// Entry table
	memcpy (rh + 0x20 + node_table_size, entry_table, entry_table_size);
	// String pool
	memcpy (rh + 0x20 + node_table_size + entry_table_size, str_pool_buf, str_pool_aligned);
	// File data
	memcpy (out + 0x20 + header_data_size, file_data_buf, cur_file_offset);

	// Cleanup
	FREE (node_table);
	FREE (entry_table);
	FREE (str_pool_buf);
	FREE (file_data_buf);
	FREE (entry_data_offsets);
	for (uint i = 0; i < num_nodes; i++)
	{
		FREE (nodes[i].name);
		FREE (nodes[i].child_indices);
	}

	*dest = out;
	*dest_size = total_rarc_size;
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    END				///////////////
///////////////////////////////////////////////////////////////////////////////
