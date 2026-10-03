#include "lib-atlusndx.h"
#include <string.h>

bool IsAtlusNdx (const u8 *data, uint data_size, u64 file_size)
{
	if (!data || data_size < 16)
		return false;

	const u16 root_count = (u16)data[0] | ((u16)data[1] << 8);
	if (root_count == 0 || root_count > 256)
		return false;

	size_t pos = 2;
	u32 first_child_off = 0;

	for (uint i = 0; i < root_count; i++)
	{
		if (pos + 6 > data_size)
			return false;

		const u16 name_len = (u16)data[pos] | ((u16)data[pos + 1] << 8);
		pos += 2;
		if (name_len == 0 || name_len > 128)
			return false;

		if (pos + name_len + 4 > data_size)
			return false;

		// Check name characters are printable ASCII
		for (uint c = 0; c < name_len; c++)
		{
			const u8 ch = data[pos + c];
			if (ch < 0x20 || ch > 0x7e)
				return false;
		}
		pos += name_len;

		const u32 child_off = (u32)data[pos] | ((u32)data[pos + 1] << 8) |
		                      ((u32)data[pos + 2] << 16) | ((u32)data[pos + 3] << 24);
		pos += 4;

		if (child_off != 0)
		{
			if (file_size && (u64)child_off >= file_size)
				return false;
			if (!first_child_off)
				first_child_off = child_off;
		}
	}

	// First directory offset should not overlap with root entries table
	if (first_child_off && first_child_off < pos)
		return false;

	return true;
}

static u32 AtlusHash (const char *path, u32 bucket_count)
{
	if (!path || !*path)
		return 0;

	const u8 *p = (const u8 *)path;
	u8 c0 = *p++;
	if (c0 == '\\')
		c0 = '/';
	if (c0 >= 'A' && c0 <= 'Z')
		c0 += 0x20;

	u32 h = (c0 == '/') ? 0 : c0;
	while (*p)
	{
		u8 c = *p++;
		if (c == '\\')
			c = '/';
		if (c >= 'A' && c <= 'Z')
			c += 0x20;
		h = (h * 37 + c) & 0xffffffff;
	}
	return h & (bucket_count - 1);
}

bool AtlusIdxLookup (
	const u8 *idx,
	size_t idx_size,
	const char *path,
	u32 *out_offset,
	u32 *out_size
)
{
	if (!idx || idx_size < 8 || !path || !out_offset || !out_size)
		return false;

	const u16 bucket_count = (u16)idx[0] | ((u16)idx[1] << 8);
	if (bucket_count == 0)
		return false;

	const size_t table_size = 8 + (size_t)bucket_count * 6;
	if (idx_size < table_size)
		return false;

	const u32 h = AtlusHash (path, bucket_count);
	const u8 *ent = idx + 8 + h * 6;

	const u32 w0 = (u32)ent[0] | ((u32)ent[1] << 8) | ((u32)ent[2] << 16) | ((u32)ent[3] << 24);
	const u16 w4 = (u16)ent[4] | ((u16)ent[5] << 8);
	const u32 default_sz = (((u32)w4 << 16) >> 10) + (w0 >> 26);

	if ((w0 & 1) == 0)
	{
		*out_offset = (((w0 << 6) & 0xffffffff) >> 7) << 2;
		*out_size = default_sz;
		return true;
	}

	const size_t list_off = ((w0 << 6) & 0xffffffff) >> 7;
	if (list_off >= idx_size)
		return false;

	const u8 item_cnt = idx[list_off];
	size_t pos = list_off + 1;
	const size_t path_len = strlen (path);

	for (uint i = 0; i < item_cnt; i++)
	{
		if (pos + 4 > idx_size)
			return false;

		const u32 ent_off = ((u32)idx[pos] | ((u32)idx[pos + 1] << 8) |
		                     ((u32)idx[pos + 2] << 16) | ((u32)idx[pos + 3] << 24)) << 2;
		u32 ent_sz;
		size_t chk;

		if (i == 0)
		{
			ent_sz = default_sz;
			chk = pos + 4;
		}
		else
		{
			if (pos + 8 > idx_size)
				return false;
			ent_sz = (u32)idx[pos + 4] | ((u32)idx[pos + 5] << 8) |
			         ((u32)idx[pos + 6] << 16) | ((u32)idx[pos + 7] << 24);
			chk = pos + 8;
		}

		bool matched = true;
		while (chk < idx_size)
		{
			const u8 ch = idx[chk++];
			if (ch == 0)
				break;
			if (chk >= idx_size)
				return false;
			const u8 str_idx = idx[chk++];
			if (str_idx >= path_len)
			{
				matched = false;
				continue;
			}
			char pc = path[str_idx];
			if (pc == '\\')
				pc = '/';
			if (pc >= 'A' && pc <= 'Z')
				pc += 0x20;
			char target_c = (char)ch;
			if (target_c >= 'A' && target_c <= 'Z')
				target_c += 0x20;
			if (pc != target_c)
				matched = false;
		}

		pos = chk;
		if (matched)
		{
			*out_offset = ent_off;
			*out_size = ent_sz;
			return true;
		}
	}

	return false;
}
