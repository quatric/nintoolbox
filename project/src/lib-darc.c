#include "lib-std.h"
#include "lib-archive-util.h"
#include "lib-darc.h"
#include <string.h>
#include <errno.h>

static enumError darc_utf16le_to_utf8 (char **dest, const u8 *p, size_t size)
{
	// Require a complete, terminated UTF-16 string before allocating it.
	size_t units = 0;
	while (units < size / 2 && rd_le16 (p + 2 * units))
		units++;
	if (units == size / 2)
		return ERR_INVALID_DATA;
	char *out = MALLOC (3 * units + 1);
	if (!out)
		return ERR_OUT_OF_MEMORY;
	size_t len = 0;
	for (size_t i = 0; i < units; i++)
	{
		u32 cp = rd_le16 (p + 2 * i);
		if (cp >= 0xd800 && cp <= 0xdbff)
		{
			if (++i >= units)
				goto invalid;
			const u16 lo = rd_le16 (p + 2 * i);
			if (lo < 0xdc00 || lo > 0xdfff)
				goto invalid;
			cp = 0x10000 + ((cp - 0xd800) << 10) + lo - 0xdc00;
		}
		else if (cp >= 0xdc00 && cp <= 0xdfff)
			goto invalid;
		if (cp < 0x80)
			out[len++] = cp;
		else if (cp < 0x800)
		{
			out[len++] = 0xc0 | (cp >> 6);
			out[len++] = 0x80 | (cp & 0x3f);
		}
		else if (cp < 0x10000)
		{
			out[len++] = 0xe0 | (cp >> 12);
			out[len++] = 0x80 | ((cp >> 6) & 0x3f);
			out[len++] = 0x80 | (cp & 0x3f);
		}
		else
		{
			out[len++] = 0xf0 | (cp >> 18);
			out[len++] = 0x80 | ((cp >> 12) & 0x3f);
			out[len++] = 0x80 | ((cp >> 6) & 0x3f);
			out[len++] = 0x80 | (cp & 0x3f);
		}
	}
	out[len] = 0;
	*dest = out;
	return ERR_OK;
invalid:
	FREE (out);
	return ERR_INVALID_DATA;
}

void ResetDARC (darc_t *darc)
{
	if (!darc)
		return;
	if (darc->entries)
		for (uint i = 0; i < darc->n_entries; i++)
			FREE (darc->entries[i].name);
	FREE (darc->entries);
	memset (darc, 0, sizeof (*darc));
}

enumError ScanDARC (darc_t *darc, const u8 *data, uint size)
{
	if (!darc)
		return ERR_INVALID_DATA;
	memset (darc, 0, sizeof (*darc));
	if (!data || size < 0x1c || memcmp (data, "darc", 4) || rd_le16 (data + 4) != 0xfeff)
		return ERR_INVALID_DATA;

	const uint header_size = rd_le16 (data + 6);
	const uint file_size = rd_le32 (data + 0xc);
	const uint table_offset = rd_le32 (data + 0x10);
	const uint table_size = rd_le32 (data + 0x14);
	const uint data_offset = rd_le32 (data + 0x18);
	if (header_size < 0x1c || file_size < header_size || file_size > size
		|| table_offset < header_size || table_offset > file_size || table_size < 12
		|| table_size > file_size - table_offset || data_offset < table_offset + table_size
		|| data_offset > file_size)
		return ERR_INVALID_DATA;

	const u8 *table = data + table_offset;
	const uint n_entries = rd_le32 (table + 8);
	if (!n_entries || n_entries > table_size / 12 || rd_le32 (table + 4) != 0
		|| (rd_le32 (table) >> 24) != 1)
		return ERR_INVALID_DATA;
	const uint name_size = table_size - n_entries * 12;
	const u8 *names = table + n_entries * 12;
	darc_entry_t *entries = CALLOC (n_entries, sizeof (*entries));
	uint *stack = CALLOC (n_entries, sizeof (*stack));
	if (!entries || !stack)
	{
		FREE (entries);
		FREE (stack);
		return ERR_OUT_OF_MEMORY;
	}
	darc->entries = entries;
	darc->n_entries = n_entries;
	uint sp = 0;
	enumError err = ERR_OK;
	for (uint i = 0; i < n_entries; i++)
	{
		const u8 *e = table + i * 12;
		const u32 f0 = rd_le32 (e);
		const u32 f1 = rd_le32 (e + 4);
		const u32 f2 = rd_le32 (e + 8);
		const uint name_off = f0 & 0xffffff;
		if ((f0 >> 24) > 1 || name_off & 1 || name_off >= name_size || name_size - name_off < 2)
		{
			err = ERR_INVALID_DATA;
			break;
		}
		entries[i].is_dir = (f0 >> 24) == 1;
		entries[i].parent_or_offset = f1;
		entries[i].end_or_size = f2;
		err = darc_utf16le_to_utf8 (&entries[i].name, names + name_off, name_size - name_off);
		if (err)
			break;
		if (i)
		{
			while (sp && i >= entries[stack[sp]].end_or_size)
				sp--;
			if (entries[i].is_dir)
			{
				const uint parent = stack[sp];
				if (f1 != parent || f2 <= i || f2 > entries[parent].end_or_size)
				{
					err = ERR_INVALID_DATA;
					break;
				}
				stack[++sp] = i;
			}
			else if (f1 < data_offset || f1 > file_size || f2 > file_size - f1)
			{
				err = ERR_INVALID_DATA;
				break;
			}
		}
	}
	FREE (stack);
	if (err)
	{
		ResetDARC (darc);
		return err;
	}
	darc->data = data;
	darc->size = file_size;
	return ERR_OK;
}

typedef struct darc_build_node_t
{
	char *name;
	bool is_dir;
	uint parent;
	uint first_child;
	uint last_child;
	uint next_sibling;
	uint end;
	uint table_idx;
	uint input_idx;
	uint name_off;
	uint name_size;
	uint data_off;
} darc_build_node_t;

// Convert a validated UTF-8 component to UTF-16LE, including its terminator.
// A null output measures the encoded length before allocating the archive.
static bool darc_utf8_to_utf16le (u8 *out, uint *size, ccp name)
{
	const u8 *p = (const u8 *)name;
	uint len = 0;
	while (*p)
	{
		u32 cp = *p++;
		uint extra = 0;
		u32 minimum = 0;
		if (cp >= 0xc2 && cp <= 0xdf)
		{
			cp &= 0x1f;
			extra = 1;
			minimum = 0x80;
		}
		else if (cp >= 0xe0 && cp <= 0xef)
		{
			cp &= 0x0f;
			extra = 2;
			minimum = 0x800;
		}
		else if (cp >= 0xf0 && cp <= 0xf4)
		{
			cp &= 7;
			extra = 3;
			minimum = 0x10000;
		}
		else if (cp >= 0x80)
			return false;
		for (uint i = 0; i < extra; i++)
		{
			if ((*p & 0xc0) != 0x80)
				return false;
			cp = (cp << 6) | (*p++ & 0x3f);
		}
		if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
			return false;
		if (cp >= 0x10000)
		{
			cp -= 0x10000;
			if (out)
			{
				wr_le16 (out + len, 0xd800 | (cp >> 10));
				wr_le16 (out + len + 2, 0xdc00 | (cp & 0x3ff));
			}
			len += 4;
		}
		else
		{
			if (out)
				wr_le16 (out + len, cp);
			len += 2;
		}
	}
	if (out)
		wr_le16 (out + len, 0);
	*size = len + 2;
	return true;
}

static bool darc_valid_component (ccp name)
{
	if (!*name || !strcmp (name, ".") || !strcmp (name, ".."))
		return false;
	for (const u8 *p = (const u8 *)name; *p; p++)
		if (*p < 0x20 || *p == 0x7f || *p == ':' || *p == '\\')
			return false;
	uint size;
	return darc_utf8_to_utf16le (0, &size, name);
}

enumError CreateDARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (dest)
		*dest = 0;
	if (dest_size)
		*dest_size = 0;
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	// One root, one node per input file, and at most one node per slash.
	// Shared directories reduce this upper bound during tree construction.
	u64 capacity = 1ull + n_entries;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = entries[i].name ? entries[i].name : "file";
		if (!*name || strlen (name) >= PATH_MAX || (entries[i].size && !entries[i].data))
			return ERR_INVALID_DATA;
		for (ccp p = name; *p; p++)
			if (*p == '/')
				capacity++;
	}
	if (capacity > UINT_MAX / 12 || capacity > SIZE_MAX / sizeof (darc_build_node_t))
		return ERR_FILE_TOO_BIG;
	darc_build_node_t *nodes = CALLOC ((size_t)capacity, sizeof (*nodes));
	uint *order = MALLOC ((size_t)capacity * sizeof (*order));
	enumError err = ERR_OUT_OF_MEMORY;
	uint used = 1;
	if (!nodes || !order)
		goto cleanup;
	nodes[0].is_dir = true;
	nodes[0].name = STRDUP (".");
	if (!nodes[0].name)
		goto cleanup;

	for (uint i = 0; i < n_entries; i++)
	{
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s", entries[i].name ? entries[i].name : "file");
		uint parent = 0;
		char *component = path;
		for (;;)
		{
			char *slash = strchr (component, '/');
			if (slash)
				*slash = 0;
			if (!darc_valid_component (component))
			{
				err = ERR_INVALID_DATA;
				goto cleanup;
			}
			uint found = 0;
			for (uint c = nodes[parent].first_child; c; c = nodes[c].next_sibling)
				if (!strcmp (nodes[c].name, component))
				{
					found = c;
					break;
				}
			if (found)
			{
				// A member cannot overwrite a file or reinterpret it as a directory.
				if (!slash || !nodes[found].is_dir)
				{
					err = ERR_INVALID_DATA;
					goto cleanup;
				}
			}
			else
			{
				found = used++;
				nodes[found].name = STRDUP (component);
				if (!nodes[found].name)
				{
					err = ERR_OUT_OF_MEMORY;
					goto cleanup;
				}
				nodes[found].is_dir = slash != 0;
				nodes[found].parent = parent;
				nodes[found].input_idx = i;
				if (nodes[parent].last_child)
					nodes[nodes[parent].last_child].next_sibling = found;
				else
					nodes[parent].first_child = found;
				nodes[parent].last_child = found;
			}
			if (!slash)
				break;
			parent = found;
			component = slash + 1;
		}
	}

	// Flatten iteratively so deeply nested input never consumes the call stack.
	uint count = 0, cur = 0;
	for (;;)
	{
		nodes[cur].table_idx = count;
		order[count++] = cur;
		if (nodes[cur].first_child)
		{
			cur = nodes[cur].first_child;
			continue;
		}
		for (;;)
		{
			nodes[cur].end = count;
			if (!cur)
				goto flattened;
			if (nodes[cur].next_sibling)
			{
				cur = nodes[cur].next_sibling;
				break;
			}
			cur = nodes[cur].parent;
		}
	}
flattened:;
	u64 name_size = 0;
	for (uint i = 0; i < count; i++)
	{
		darc_build_node_t *node = nodes + order[i];
		if (name_size > 0xffffff)
		{
			err = ERR_FILE_TOO_BIG;
			goto cleanup;
		}
		node->name_off = name_size;
		darc_utf8_to_utf16le (0, &node->name_size, node->name);
		name_size += node->name_size;
	}
	const u64 table_size = 12ull * count + ((name_size + 3) & ~3ull);
	const u64 data_start = (0x1c + table_size + 0x7f) & ~0x7full;
	u64 total = data_start;
	for (uint i = 0; i < count; i++)
	{
		darc_build_node_t *node = nodes + order[i];
		if (!node->is_dir)
		{
			if (total > UINT_MAX)
			{
				err = ERR_FILE_TOO_BIG;
				goto cleanup;
			}
			node->data_off = total;
			total += ((u64)entries[node->input_idx].size + 3) & ~3ull;
		}
	}
	if (total > UINT_MAX)
	{
		err = ERR_FILE_TOO_BIG;
		goto cleanup;
	}
	u8 *out = CALLOC (1, (size_t)total);
	if (!out)
	{
		err = ERR_OUT_OF_MEMORY;
		goto cleanup;
	}
	memcpy (out, "darc", 4);
	wr_le16 (out + 4, 0xfeff);
	wr_le16 (out + 6, 0x1c);
	wr_le32 (out + 8, 0x01000000);
	wr_le32 (out + 0xc, total);
	wr_le32 (out + 0x10, 0x1c);
	wr_le32 (out + 0x14, table_size);
	wr_le32 (out + 0x18, data_start);
	u8 *names = out + 0x1c + 12 * count;
	for (uint i = 0; i < count; i++)
	{
		darc_build_node_t *node = nodes + order[i];
		u8 *entry = out + 0x1c + 12 * i;
		wr_le32 (entry, node->name_off | (node->is_dir ? 0x01000000 : 0));
		if (node->is_dir)
		{
			wr_le32 (entry + 4, nodes[node->parent].table_idx);
			wr_le32 (entry + 8, node->end);
		}
		else
		{
			const nintendo_sarc_entry_t *input = entries + node->input_idx;
			wr_le32 (entry + 4, node->data_off);
			wr_le32 (entry + 8, input->size);
			if (input->size)
				memcpy (out + node->data_off, input->data, input->size);
		}
		darc_utf8_to_utf16le (names + node->name_off, &node->name_size, node->name);
	}
	*dest = out;
	*dest_size = total;
	err = ERR_OK;
cleanup:
	if (nodes)
		for (uint i = 0; i < used; i++)
			FREE (nodes[i].name);
	FREE (nodes);
	FREE (order);
	return err;
}

//-----------------------------------------------------------------------------
///////////////		Level-5 / Layton Archive (DARC)			///////////////
//-----------------------------------------------------------------------------

enumError DecodeDARC (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 12 || memcmp (src, "DARC", 4))
		return EINVAL;

	const u32 num_files = rd_le32 (src + 4);
	if (!num_files || (u64)8 + (u64)num_files * 4 > src_size)
		return EINVAL;

	const u32 rel_ofs0 = rd_le32 (src + 8);
	const u64 abs_ofs0 = (u64)8 + 4 + rel_ofs0;
	if (abs_ofs0 >= 4 && abs_ofs0 <= src_size)
	{
		const u32 sz0 = rd_le32 (src + abs_ofs0 - 4);
		if (sz0 > 0 && abs_ofs0 + sz0 <= src_size)
		{
			u8 *out = MALLOC (sz0);
			if (!out)
				return ERR_CANT_CREATE;
			memcpy (out, src + abs_ofs0, sz0);
			*dest = out;
			*dest_size = sz0;
			return ERR_OK;
		}
	}
	return EINVAL;
}

enumError create_darc_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;
	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateDARC (&data, &size, list.entry, list.used);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, dest);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		const enumError close_err = ResetFile (&F, opt_preserve);
		if (close_err > err)
			err = close_err;
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}
