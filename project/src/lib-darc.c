#include "lib-std.h"
#include "lib-darc.h"
#include <string.h>
#include <errno.h>

static char *darc_utf16le_to_utf8 (const u8 *p, const u8 *end)
{
	uint cap = 4, len = 0;
	char *out = MALLOC (cap);
	if (!out)
		return 0;
	while (p + 2 <= end)
	{
		const u16 u = rd_le16 (p);
		p += 2;
		if (!u)
			break;
		u32 cp = u;
		if (u >= 0xD800 && u <= 0xDBFF && p + 2 <= end)
		{
			const u16 lo = rd_le16 (p);
			if (lo >= 0xDC00 && lo <= 0xDFFF)
			{
				cp = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
				p += 2;
			}
			else
				cp = 0xFFFD;
		}
		else if (u >= 0xD800 && u <= 0xDFFF)
			cp = 0xFFFD;

		char enc[4];
		uint n;
		if (cp < 0x80)
		{
			enc[0] = (char)cp;
			n = 1;
		}
		else if (cp < 0x800)
		{
			enc[0] = 0xC0 | (cp >> 6);
			enc[1] = 0x80 | (cp & 0x3f);
			n = 2;
		}
		else if (cp < 0x10000)
		{
			enc[0] = 0xE0 | (cp >> 12);
			enc[1] = 0x80 | ((cp >> 6) & 0x3f);
			enc[2] = 0x80 | (cp & 0x3f);
			n = 3;
		}
		else
		{
			enc[0] = 0xF0 | (cp >> 18);
			enc[1] = 0x80 | ((cp >> 12) & 0x3f);
			enc[2] = 0x80 | ((cp >> 6) & 0x3f);
			enc[3] = 0x80 | (cp & 0x3f);
			n = 4;
		}

		if (len + n + 1 > cap)
		{
			cap = (len + n + 1) * 2;
			char *grown = REALLOC (out, cap);
			if (!grown)
			{
				FREE (out);
				return 0;
			}
			out = grown;
		}
		memcpy (out + len, enc, n);
		len += n;
	}
	out[len] = 0;
	return out;
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
	if (!darc || !data || size < 0x1c || memcmp (data, "darc", 4))
		return EINVAL;
	if (rd_le16 (data + 4) != 0xfeff)
		return EINVAL;

	const uint header_size = rd_le16 (data + 6);
	const uint file_size = rd_le32 (data + 0xc);
	const uint table_offset = rd_le32 (data + 0x10);
	const uint table_size = rd_le32 (data + 0x14);
	if (header_size < 0x1c || file_size > size || table_offset < header_size || table_size < 12
		|| (u64)table_offset + table_size > size)
		return EINVAL;

	const uint n = table_size / 12;
	if (!n || (u64)table_offset + 12 > size)
		return EINVAL;

	const u32 e0 = rd_le32 (data + table_offset);
	if (!(e0 & 0x01000000))
		return EINVAL;
	const uint n_entries = rd_le32 (data + table_offset + 8);
	if (!n_entries || n_entries > n || (u64)table_offset + (u64)n_entries * 12 > size)
		return EINVAL;

	const u8 *name_area = data + table_offset + n_entries * 12;
	const u8 *name_area_end
		= data + (table_offset + table_size <= size ? table_offset + table_size : size);

	darc_entry_t *entries = CALLOC (n_entries, sizeof (*entries));
	if (!entries)
		return ERR_CANT_CREATE;

	for (uint i = 0; i < n_entries; i++)
	{
		const u8 *e = data + table_offset + i * 12;
		const u32 f0 = rd_le32 (e);
		const u32 f1 = rd_le32 (e + 4);
		const u32 f2 = rd_le32 (e + 8);
		const bool is_dir = (f0 & 0x01000000) != 0;
		const uint name_off = f0 & 0xffffff;

		entries[i].is_dir = is_dir;
		entries[i].parent_or_offset = f1;
		entries[i].end_or_size = f2;

		if (name_area + name_off < name_area_end)
			entries[i].name = darc_utf16le_to_utf8 (name_area + name_off, name_area_end);
		if (!entries[i].name)
			entries[i].name = STRDUP ("");

		if (!is_dir && ((u64)f1 + f2 > size))
		{
			for (uint k = 0; k <= i; k++)
				FREE (entries[k].name);
			FREE (entries);
			return EINVAL;
		}
	}

	darc->data = data;
	darc->size = size;
	darc->entries = entries;
	darc->n_entries = n_entries;
	return ERR_OK;
}

typedef struct darc_build_node_t
{
	char *name;
	bool is_dir;
	uint parent_node_idx;
	uint end_subtree_idx;
	uint table_idx;
	uint orig_entry_idx;
	uint name_off;
	uint data_off;
	uint data_size;
	uint num_children;
	uint *child_indices;
} darc_build_node_t;

static void flatten_darc_node (
	darc_build_node_t *nodes, uint cur_node, uint *order, uint *order_count)
{
	uint my_table_idx = (*order_count)++;
	nodes[cur_node].table_idx = my_table_idx;
	order[my_table_idx] = cur_node;

	for (uint c = 0; c < nodes[cur_node].num_children; c++)
	{
		uint child = nodes[cur_node].child_indices[c];
		if (nodes[child].is_dir)
			flatten_darc_node (nodes, child, order, order_count);
		else
		{
			uint f_table_idx = (*order_count)++;
			nodes[child].table_idx = f_table_idx;
			order[f_table_idx] = child;
		}
	}

	nodes[cur_node].end_subtree_idx = *order_count;
}

enumError CreateDARC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xFFFF)
		return EINVAL;

	darc_build_node_t nodes[1024];
	uint num_nodes = 1;
	memset (nodes, 0, sizeof (nodes));
	nodes[0].name = STRDUP (".");
	nodes[0].is_dir = true;
	nodes[0].parent_node_idx = 0;

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
			nodes[new_f].orig_entry_idx = i;
			nodes[new_f].data_size = entries[i].size;
			nodes[cur_dir].child_indices = REALLOC (
				nodes[cur_dir].child_indices, (nodes[cur_dir].num_children + 1) * sizeof (uint));
			nodes[cur_dir].child_indices[nodes[cur_dir].num_children++] = new_f;
		}
	}

	uint order[1024];
	uint order_count = 0;
	flatten_darc_node (nodes, 0, order, &order_count);

	uint name_cap = 4096;
	u8 *name_table = MALLOC (name_cap);
	if (!name_table)
	{
		for (uint n = 0; n < num_nodes; n++)
		{
			FREE (nodes[n].name);
			FREE (nodes[n].child_indices);
		}
		return ERR_CANT_CREATE;
	}
	uint name_pos = 0;

	for (uint i = 0; i < order_count; i++)
	{
		uint nidx = order[i];
		nodes[nidx].name_off = name_pos;
		ccp nstr = nodes[nidx].name;
		size_t nlen = strlen (nstr);

		while (name_pos + 2 * nlen + 2 > name_cap)
		{
			name_cap *= 2;
			name_table = REALLOC (name_table, name_cap);
		}

		for (size_t c = 0; c < nlen; c++)
		{
			wr_le16 (name_table + name_pos, (u16)(u8)nstr[c]);
			name_pos += 2;
		}
		wr_le16 (name_table + name_pos, 0);
		name_pos += 2;
	}

	uint name_table_size = (name_pos + 3) & ~3u;

	const uint table_size = 12 * order_count + name_table_size;
	uint cur_data_off = (0x1C + table_size + 0x7F) & ~0x7Fu;
	const uint data_base_off = cur_data_off;

	for (uint i = 0; i < order_count; i++)
	{
		uint nidx = order[i];
		if (!nodes[nidx].is_dir)
		{
			nodes[nidx].data_off = cur_data_off;
			cur_data_off += (nodes[nidx].data_size + 3) & ~3u;
		}
	}

	const uint total_file_size = cur_data_off;
	u8 *out = CALLOC (1, total_file_size);
	if (!out)
	{
		FREE (name_table);
		for (uint n = 0; n < num_nodes; n++)
		{
			FREE (nodes[n].name);
			FREE (nodes[n].child_indices);
		}
		return ERR_CANT_CREATE;
	}

	memcpy (out, "darc", 4);
	wr_le16 (out + 4, 0xFEFF);
	wr_le16 (out + 6, 0x001C);
	wr_le32 (out + 8, 0x01000000);
	wr_le32 (out + 0x0C, total_file_size);
	wr_le32 (out + 0x10, 0x0000001C);
	wr_le32 (out + 0x14, table_size);
	wr_le32 (out + 0x18, data_base_off);

	for (uint i = 0; i < order_count; i++)
	{
		uint nidx = order[i];
		u8 *e = out + 0x1C + 12 * i;
		if (nodes[nidx].is_dir)
		{
			uint parent_tidx = nodes[nodes[nidx].parent_node_idx].table_idx;
			wr_le32 (e + 0, 0x01000000 | (nodes[nidx].name_off & 0x00FFFFFF));
			wr_le32 (e + 4, parent_tidx);
			wr_le32 (e + 8, nodes[nidx].end_subtree_idx);
		}
		else
		{
			wr_le32 (e + 0, nodes[nidx].name_off & 0x00FFFFFF);
			wr_le32 (e + 4, nodes[nidx].data_off);
			wr_le32 (e + 8, nodes[nidx].data_size);
		}
	}

	memcpy (out + 0x1C + 12 * order_count, name_table, name_pos);
	FREE (name_table);

	for (uint i = 0; i < order_count; i++)
	{
		uint nidx = order[i];
		if (!nodes[nidx].is_dir && nodes[nidx].data_size)
		{
			uint oidx = nodes[nidx].orig_entry_idx;
			if (entries[oidx].data)
				memcpy (out + nodes[nidx].data_off, entries[oidx].data, nodes[nidx].data_size);
		}
	}

	for (uint n = 0; n < num_nodes; n++)
	{
		FREE (nodes[n].name);
		FREE (nodes[n].child_indices);
	}

	*dest = out;
	*dest_size = total_file_size;
	return ERR_OK;
}
