// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Nordcurrent BigFile PK2 Multi-Part Archive (.pk2)
// Used in Wii titles developed by Nordcurrent (101-in-1 Party Megamix, etc.)
//-----------------------------------------------------------------------------
#include "lib-pk2.h"
#include "lib-std.h"
#include "lib-archive-util.h"

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

bool IsPK2 (const u8 *data, size_t size)
{
	if (!data || size < 0x10)
		return false;

	if (data[0] != 1 || data[1] != 1)
		return false;

	const u8 num_parts = data[2];
	if (num_parts < 1 || num_parts > 0x20)
		return false;

	const u16 ext_len = (u16)data[3] | ((u16)data[4] << 8);
	if (ext_len < 6 || ext_len >= size)
		return false;

	if (data[ext_len - 1] != 0)
		return false;

	for (size_t i = 5; i < ext_len - 1; i++)
	{
		const u8 c = data[i];
		if (c != 0 && (c < 'A' || c > 'Z') && (c < '0' || c > '9') && c != '_')
			return false;
	}

	return true;
}

static void parse_pk2_trie (const u8 *data, ccp const *ext_tab, uint num_exts,
                           size_t pos, size_t end,
                           char *path_buf, size_t path_len,
                           FILE **slices, uint num_slices,
                           ccp dest_dir, uint *file_count)
{
	while (pos < end)
	{
		u64 child_len = 0;
		while (pos < end)
		{
			const u8 b = data[pos++];
			child_len = (child_len << 7) | (b & 0x7f);
			if (!(b & 0x80))
				break;
		}
		if (child_len == 0)
			break;

		size_t node_end = pos + child_len;
		if (node_end > end)
			node_end = end;

		const size_t name_start = pos;
		while (pos < node_end && data[pos] != 0)
			pos++;

		const size_t name_len = pos - name_start;
		if (pos >= node_end)
			break;

		pos++; // skip null terminator

		if (pos < node_end && data[pos] != 0)
		{
			// Internal node
			const size_t old_len = path_len;
			if (path_len + name_len < 1024)
			{
				memcpy (path_buf + path_len, data + name_start, name_len);
				path_len += name_len;
				path_buf[path_len] = 0;
			}
			parse_pk2_trie (data, ext_tab, num_exts, pos, node_end,
			                path_buf, path_len, slices, num_slices, dest_dir, file_count);
			path_buf[old_len] = 0;
			path_len = old_len;
		}
		else
		{
			// Leaf node: last char of name is 1-based extension index
			const u8 ext_idx = name_len > 0 ? data[name_start + name_len - 1] : 0;
			const size_t base_len = name_len > 0 ? name_len - 1 : 0;
			ccp ext = (ext_idx > 0 && ext_idx <= num_exts && ext_tab[ext_idx])
			              ? ext_tab[ext_idx] : "";

			if (pos + 13 <= end)
			{
				// pos[0] is leaf flag (0)
				const u32 off_lo = *(const u32 *)(data + pos + 1);
				const u32 off_hi = *(const u32 *)(data + pos + 5);
				u32 size = *(const u32 *)(data + pos + 9);
				u64 offset = (u64)off_lo | ((u64)off_hi << 32);

				char rel_path[1024];
				if (*ext)
					snprintf (rel_path, sizeof (rel_path), "%.*s%.*s.%s",
					          (int)path_len, path_buf, (int)base_len, (ccp)(data + name_start), ext);
				else
					snprintf (rel_path, sizeof (rel_path), "%.*s%.*s",
					          (int)path_len, path_buf, (int)base_len, (ccp)(data + name_start));

				for (char *p = rel_path; *p; p++)
					if (*p == '\\')
						*p = '/';

				char full_dest[2048];
				snprintf (full_dest, sizeof (full_dest), "%s/%s", dest_dir, rel_path);

				if (file_count)
					(*file_count)++;

				if (verbose >= 0 && size == 0)
				{
					// empty file or marker
				}

				CreatePath (full_dest, false);

				FILE *out = fopen (full_dest, "wb");
				if (out)
				{
					u32 remaining = size;
					u64 cur_off = offset;
					u8 chunk[65536];

					while (remaining > 0)
					{
						const uint slice_idx = (uint)(cur_off >> 30);
						const u32 in_slice_off = (u32)(cur_off & 0x3fffffff);
						if (slice_idx >= num_slices || !slices[slice_idx])
							break;

						const u32 avail = 0x40000000 - in_slice_off;
						u32 to_read = (remaining < avail) ? remaining : avail;

						fseek (slices[slice_idx], in_slice_off, SEEK_SET);
						while (to_read > 0)
						{
							const u32 take = (to_read < sizeof (chunk)) ? to_read : (u32)sizeof (chunk);
							const size_t rd = fread (chunk, 1, take, slices[slice_idx]);
							if (!rd)
								break;
							fwrite (chunk, 1, rd, out);
							to_read -= (u32)rd;
							cur_off += rd;
							remaining -= (u32)rd;
						}
					}
					fclose (out);
				}
			}
		}
		pos = node_end;
	}
}

enumError ExtractPK2Archive (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!arg || !*arg)
		return ERR_NOTHING_TO_DO;

	const size_t arg_len = strlen (arg);
	if (arg_len < 4 || strcasecmp (arg + arg_len - 4, ".pk2"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err || !raw)
		return ERR_NOTHING_TO_DO;

	if (!IsPK2 (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u8 num_parts = raw[2];
	const u16 ext_len = (u16)raw[3] | ((u16)raw[4] << 8);

	ccp ext_tab[256] = { 0 };
	uint num_exts = 0;
	size_t pos = 5;
	while (pos < ext_len && pos < raw_size)
	{
		if (raw[pos] == 0)
		{
			pos++;
			continue;
		}
		if (num_exts < 255)
			ext_tab[++num_exts] = (ccp)(raw + pos);
		pos += strlen ((ccp)(raw + pos)) + 1;
	}

	FILE *slices[32] = { 0 };
	char base_no_ext[1024];
	const int base_len = (arg_len >= 4) ? (int)(arg_len - 4) : (int)arg_len;

	const uint open_count = num_parts < 32 ? num_parts : 32;
	for (uint i = 0; i < open_count; i++)
	{
		snprintf (base_no_ext, sizeof (base_no_ext), "%.*s.p%02u", base_len, arg, i);
		slices[i] = fopen (base_no_ext, "rb");
		if (!slices[i])
		{
			snprintf (base_no_ext, sizeof (base_no_ext), "%.*s.P%02u", base_len, arg, i);
			slices[i] = fopen (base_no_ext, "rb");
		}
	}

	char dest[1024];
	if (opt_dest && *opt_dest)
		snprintf (dest, sizeof (dest), "%s", opt_dest);
	else if (basedir && *basedir)
		snprintf (dest, sizeof (dest), "%s", basedir);
	else
		snprintf (dest, sizeof (dest), "%.*s.d", base_len, arg);

	CreatePath (dest, true);

	if (verbose >= 0)
		fprintf (stdlog, "%s%sEXTRACT PK2:%s (%u parts) -> %s/\n",
		         verbose > 0 ? "\n" : "",
		         testmode ? "WOULD " : "",
		         arg, num_parts, dest);

	if (testmode)
	{
		for (uint i = 0; i < open_count; i++)
			if (slices[i])
				fclose (slices[i]);
		FREE (raw);
		return ERR_OK;
	}

	char path_buf[1024] = { 0 };
	uint file_count = 0;
	parse_pk2_trie (raw, ext_tab, num_exts, ext_len, raw_size,
	                path_buf, 0, slices, open_count, dest, &file_count);

	for (uint i = 0; i < open_count; i++)
		if (slices[i])
			fclose (slices[i]);

	FREE (raw);
	return ERR_OK;
}

// Radix trie creation helpers for repacking
typedef struct pk2_file_item_t
{
	char *rel_path; // uppercase path with backslashes
	char *base_name;
	u8 ext_idx;
	u64 offset;
	u32 size;
	char *disk_path;
} pk2_file_item_t;

typedef struct pk2_collect_t
{
	pk2_file_item_t *items;
	uint count;
	uint alloc;
	char ext_list[256][16];
	uint ext_count;
} pk2_collect_t;

static int compare_pk2_items (const void *a, const void *b)
{
	const pk2_file_item_t *ia = (const pk2_file_item_t *)a;
	const pk2_file_item_t *ib = (const pk2_file_item_t *)b;
	return strcmp (ia->rel_path, ib->rel_path);
}

static void buf_append_varint (u8 **buf, size_t *used, size_t *alloc, u64 val)
{
	u8 tmp[10];
	int len = 0;
	tmp[len++] = (u8)(val & 0x7f);
	val >>= 7;
	while (val > 0)
	{
		tmp[len++] = (u8)((val & 0x7f) | 0x80);
		val >>= 7;
	}
	if (*used + len > *alloc)
	{
		*alloc = (*alloc == 0) ? 128 : (*alloc * 2 + len);
		*buf = REALLOC (*buf, *alloc);
	}
	for (int i = len - 1; i >= 0; i--)
		(*buf)[(*used)++] = tmp[i];
}

static void scan_pk2_dir (ccp root, ccp sub, pk2_collect_t *col)
{
	char dir_path[1024];
	if (sub && *sub)
		snprintf (dir_path, sizeof (dir_path), "%s/%s", root, sub);
	else
		snprintf (dir_path, sizeof (dir_path), "%s", root);

	DIR *dir = opendir (dir_path);
	if (!dir)
		return;

	struct dirent *entry;
	while ((entry = readdir (dir)) != 0)
	{
		if (entry->d_name[0] == '.')
			continue;

		char child_sub[1024];
		if (sub && *sub)
			snprintf (child_sub, sizeof (child_sub), "%s/%s", sub, entry->d_name);
		else
			snprintf (child_sub, sizeof (child_sub), "%s", entry->d_name);

		char full_path[1024];
		snprintf (full_path, sizeof (full_path), "%s/%s", root, child_sub);

		struct stat st;
		if (stat (full_path, &st) != 0)
			continue;

		if (S_ISDIR (st.st_mode))
		{
			scan_pk2_dir (root, child_sub, col);
		}
		else if (S_ISREG (st.st_mode))
		{
			if (col->count >= col->alloc)
			{
				col->alloc = col->alloc ? col->alloc * 2 : 128;
				col->items = REALLOC (col->items, col->alloc * sizeof (pk2_file_item_t));
			}

			pk2_file_item_t *item = &col->items[col->count++];
			memset (item, 0, sizeof (*item));
			item->disk_path = STRDUP (full_path);
			item->size = (u32)st.st_size;

			// Convert child_sub to uppercase with backslashes
			char norm[1024];
			snprintf (norm, sizeof (norm), "%s", child_sub);
			for (char *p = norm; *p; p++)
			{
				if (*p == '/')
					*p = '\\';
				else
					*p = (char)toupper ((u8)*p);
			}
			item->rel_path = STRDUP (norm);

			// Extract extension
			char *dot = strrchr (norm, '.');
			char ext_str[16] = { 0 };
			if (dot)
			{
				snprintf (ext_str, sizeof (ext_str), "%s", dot + 1);
				*dot = 0;
			}
			item->base_name = STRDUP (norm);

			u8 ext_idx = 0;
			if (ext_str[0])
			{
				for (uint i = 0; i < col->ext_count; i++)
				{
					if (!strcmp (col->ext_list[i], ext_str))
					{
						ext_idx = (u8)(i + 1);
						break;
					}
				}
				if (!ext_idx && col->ext_count < 255)
				{
					ext_idx = (u8)(++col->ext_count);
					snprintf (col->ext_list[ext_idx - 1], sizeof (col->ext_list[0]), "%s", ext_str);
				}
			}
			item->ext_idx = ext_idx;
		}
	}
	closedir (dir);
}

static void build_pk2_node (const pk2_file_item_t *items, uint start, uint end,
                           size_t prefix_len, u8 **buf, size_t *used, size_t *alloc)
{
	if (start >= end)
		return;

	// Check if this single item matches prefix_len
	if (start + 1 == end)
	{
		const pk2_file_item_t *it = &items[start];
		ccp full = it->rel_path;
		ccp remaining_name = full + prefix_len;
		size_t rem_len = strlen (remaining_name);

		// Leaf: name + ext_idx + '\0' + 0x00 + 8 bytes offset + 4 bytes size
		const size_t item_len = rem_len + 1 + 1 + 1 + 8 + 4;

		buf_append_varint (buf, used, alloc, item_len);

		if (*used + item_len > *alloc)
		{
			*alloc = *alloc * 2 + item_len;
			*buf = REALLOC (*buf, *alloc);
		}

		memcpy (*buf + *used, remaining_name, rem_len);
		*used += rem_len;
		(*buf)[(*used)++] = it->ext_idx;
		(*buf)[(*used)++] = 0; // null
		(*buf)[(*used)++] = 0; // leaf flag

		const u32 off_lo = (u32)it->offset;
		const u32 off_hi = (u32)(it->offset >> 32);
		const u32 sz = it->size;

		memcpy (*buf + *used, &off_lo, 4);
		*used += 4;
		memcpy (*buf + *used, &off_hi, 4);
		*used += 4;
		memcpy (*buf + *used, &sz, 4);
		*used += 4;
		return;
	}

	// Find common prefix among items[start..end-1] starting from prefix_len
	size_t common = 0;
	while (true)
	{
		const char c = items[start].rel_path[prefix_len + common];
		if (!c)
			break;
		bool all_match = true;
		for (uint i = start + 1; i < end; i++)
		{
			if (items[i].rel_path[prefix_len + common] != c)
			{
				all_match = false;
				break;
			}
		}
		if (!all_match)
			break;
		common++;
	}

	// Build children buffer
	u8 *child_buf = 0;
	size_t child_used = 0, child_alloc = 0;

	uint cur = start;
	while (cur < end)
	{
		if (strlen (items[cur].rel_path) == prefix_len + common)
		{
			build_pk2_node (items, cur, cur + 1, prefix_len + common,
			                &child_buf, &child_used, &child_alloc);
			cur++;
		}
		else
		{
			const char c = items[cur].rel_path[prefix_len + common];
			uint next = cur + 1;
			while (next < end && items[next].rel_path[prefix_len + common] == c)
				next++;
			build_pk2_node (items, cur, next, prefix_len + common,
			                &child_buf, &child_used, &child_alloc);
			cur = next;
		}
	}

	// Internal node length: common prefix length + 1 (null) + children size
	const size_t total_node_len = common + 1 + child_used;
	buf_append_varint (buf, used, alloc, total_node_len);

	if (*used + common + 1 + child_used > *alloc)
	{
		*alloc = *alloc * 2 + common + 1 + child_used;
		*buf = REALLOC (*buf, *alloc);
	}

	memcpy (*buf + *used, items[start].rel_path + prefix_len, common);
	*used += common;
	(*buf)[(*used)++] = 0; // null terminator

	memcpy (*buf + *used, child_buf, child_used);
	*used += child_used;

	FREE (child_buf);
}

enumError create_pk2_dir (ccp source_dir, ccp dest_file)
{
	pk2_collect_t col = { 0 };
	scan_pk2_dir (source_dir, "", &col);

	if (!col.count)
	{
		FREE (col.items);
		return ERROR0 (ERR_NOTHING_TO_DO, "Can't open PK2 input directory: %s\n", source_dir);
	}

	qsort (col.items, col.count, sizeof (pk2_file_item_t), compare_pk2_items);

	const size_t dlen = strlen (dest_file);
	const int base_len = (dlen >= 4 && !strcasecmp (dest_file + dlen - 4, ".pk2"))
	                         ? (int)(dlen - 4) : (int)dlen;

	// Write data slices (1 GiB each)
	uint slice_idx = 0;
	u64 cur_total_offset = 0;
	FILE *slice_file = 0;
	char slice_path[1024];

	for (uint i = 0; i < col.count; i++)
	{
		pk2_file_item_t *it = &col.items[i];
		FILE *in = fopen (it->disk_path, "rb");
		if (!in)
			continue;

		if (!slice_file)
		{
			snprintf (slice_path, sizeof (slice_path), "%.*s.p%02u", base_len, dest_file, slice_idx);
			slice_file = fopen (slice_path, "wb");
			if (slice_idx == 0 && slice_file)
			{
				fwrite ("PKF", 1, 3, slice_file);
				cur_total_offset = 3;
			}
		}

		it->offset = cur_total_offset;
		u8 chunk[65536];
		u32 rem = it->size;

		while (rem > 0)
		{
			const u32 in_slice = (u32)(cur_total_offset & 0x3fffffff);
			const u32 avail = 0x40000000 - in_slice;
			u32 take = (rem < avail) ? rem : avail;

			while (take > 0)
			{
				const u32 to_read = (take < sizeof (chunk)) ? take : (u32)sizeof (chunk);
				const size_t rd = fread (chunk, 1, to_read, in);
				if (!rd)
					break;
				fwrite (chunk, 1, rd, slice_file);
				take -= (u32)rd;
				rem -= (u32)rd;
				cur_total_offset += rd;
			}

			if ((cur_total_offset & 0x3fffffff) == 0 && rem > 0)
			{
				fclose (slice_file);
				slice_idx++;
				snprintf (slice_path, sizeof (slice_path), "%.*s.p%02u", base_len, dest_file, slice_idx);
				slice_file = fopen (slice_path, "wb");
			}
		}
		fclose (in);
	}
	if (slice_file)
		fclose (slice_file);

	const u8 num_slices = (u8)(slice_idx + 1);

	// Build Radix Trie
	u8 *trie_buf = 0;
	size_t trie_used = 0, trie_alloc = 0;
	build_pk2_node (col.items, 0, col.count, 0, &trie_buf, &trie_used, &trie_alloc);

	// Construct extension table
	u8 ext_tab_buf[1024];
	size_t ext_pos = 5;
	for (uint i = 0; i < col.ext_count; i++)
	{
		const size_t l = strlen (col.ext_list[i]);
		memcpy (ext_tab_buf + ext_pos, col.ext_list[i], l + 1);
		ext_pos += l + 1;
	}
	ext_tab_buf[ext_pos++] = 0; // terminating null

	ext_tab_buf[0] = 1;
	ext_tab_buf[1] = 1;
	ext_tab_buf[2] = num_slices;
	const u16 ext_len = (u16)ext_pos;
	ext_tab_buf[3] = (u8)ext_len;
	ext_tab_buf[4] = (u8)(ext_len >> 8);

	// Write output .pk2
	FILE *out = fopen (dest_file, "wb");
	if (!out)
	{
		FREE (trie_buf);
		for (uint i = 0; i < col.count; i++)
		{
			FREE (col.items[i].disk_path);
			FREE (col.items[i].rel_path);
			FREE (col.items[i].base_name);
		}
		FREE (col.items);
		return ERR_CANT_CREATE;
	}

	fwrite (ext_tab_buf, 1, ext_pos, out);
	if (trie_used)
		fwrite (trie_buf, 1, trie_used, out);
	fclose (out);

	FREE (trie_buf);
	for (uint i = 0; i < col.count; i++)
	{
		FREE (col.items[i].disk_path);
		FREE (col.items[i].rel_path);
		FREE (col.items[i].base_name);
	}
	FREE (col.items);

	return ERR_OK;
}
