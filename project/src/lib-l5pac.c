// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Level-5 / Armor Project PAC archive container implementation; see lib-l5pac.h.
//-----------------------------------------------------------------------------
#include "lib-l5pac.h"
#include "lib-std.h"
#include <string.h>
#include <stdlib.h>

static inline u32 l5pac_le32 (const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

bool IsL5Pac (const u8 *head, size_t head_size, u64 file_size)
{
	if (!head || head_size < L5PAC_HDR_SIZE)
		return false;

	// NARC archives sometimes end in .pac (e.g. param.pac); do not steal them
	if (!memcmp (head, "NARC", 4) || !memcmp (head, "CRAN", 4))
		return false;

	const u32 h_len = l5pac_le32 (head + 0x40);
	const u32 f_len = l5pac_le32 (head + 0x44);
	const u32 a_len = l5pac_le32 (head + 0x48);

	if (h_len != L5PAC_HDR_SIZE)
		return false;
	if (a_len == 0 || a_len == 0xffffffff || f_len == 0xffffffff)
		return false;
	if (file_size && a_len > file_size)
		return false;
	if (f_len > a_len)
		return false;

	// Filename inspection: null-terminated within first 40 bytes, must contain '.'
	// Characters must be printable ASCII or high bytes (Shift-JIS)
	size_t name_len = 0;
	bool has_dot = false;
	while (name_len < 40 && head[name_len] != 0)
	{
		const u8 ch = head[name_len];
		if (ch < 0x20)
			return false;
		if (ch == '.')
			has_dot = true;
		name_len++;
	}

	if (name_len == 0 || name_len >= 40 || !has_dot)
		return false;

	// If there are more entries within head_size, probe the next entry
	if (a_len + L5PAC_HDR_SIZE <= head_size)
	{
		const u8 *next = head + a_len;
		const u32 h2 = l5pac_le32 (next + 0x40);
		const u32 f2 = l5pac_le32 (next + 0x44);
		const u32 a2 = l5pac_le32 (next + 0x48);

		if (h2 == L5PAC_HDR_SIZE)
		{
			// Valid second header or terminal sentinel
			if (f2 == 0xffffffff && a2 == 0xffffffff)
				return true;
			if (a2 > 0 && f2 <= a2)
				return true;
		}
		else if (h2 == 0 && f2 == 0 && a2 == 0)
		{
			// Zeroed padding / end of archive
			return true;
		}
		// If second header doesn't match, this is not a genuine L5 PAC
		return false;
	}

	return true;
}

void L5PacFree (l5pac_t *pac)
{
	if (!pac)
		return;
	FREE (pac->e);
	memset (pac, 0, sizeof (*pac));
}

enumError L5PacParse (l5pac_t *pac, const u8 *d, size_t size, u64 file_size)
{
	if (!pac || !d)
		return ERR_INVALID_DATA;
	memset (pac, 0, sizeof (*pac));

	if (!IsL5Pac (d, size, file_size))
		return ERR_INVALID_DATA;

	const u64 total_sz = file_size ? file_size : (u64)size;

	// First pass: count entries
	uint total_entries = 0;
	u64 off = 0;
	while (off + L5PAC_HDR_SIZE <= total_sz && off + L5PAC_HDR_SIZE <= size)
	{
		const u8 *h = d + off;
		const u32 h_len = l5pac_le32 (h + 0x40);
		const u32 f_len = l5pac_le32 (h + 0x44);
		const u32 a_len = l5pac_le32 (h + 0x48);

		if (h_len != L5PAC_HDR_SIZE)
			break;
		if (a_len == 0 || a_len == 0xffffffff || f_len == 0xffffffff)
			break;
		if (off + L5PAC_HDR_SIZE + f_len > total_sz)
			break;

		// Entry filename
		size_t nlen = 0;
		while (nlen < 40 && h[nlen] != 0)
			nlen++;
		if (nlen == 0)
			break;

		total_entries++;
		off += a_len;
	}

	if (!total_entries)
		return ERR_NOTHING_TO_DO;

	pac->e = CALLOC (total_entries, sizeof (*pac->e));
	if (!pac->e)
		return ERR_OUT_OF_MEMORY;

	// Second pass: fill entries
	uint out_idx = 0;
	off = 0;
	while (off + L5PAC_HDR_SIZE <= total_sz && off + L5PAC_HDR_SIZE <= size && out_idx < total_entries)
	{
		const u8 *h = d + off;
		const u32 h_len = l5pac_le32 (h + 0x40);
		const u32 f_len = l5pac_le32 (h + 0x44);
		const u32 a_len = l5pac_le32 (h + 0x48);

		if (h_len != L5PAC_HDR_SIZE)
			break;
		if (a_len == 0 || a_len == 0xffffffff || f_len == 0xffffffff)
			break;
		if (off + L5PAC_HDR_SIZE + f_len > total_sz)
			break;

		size_t nlen = 0;
		while (nlen < 40 && h[nlen] != 0)
			nlen++;
		if (nlen == 0)
			break;

		memcpy (pac->e[out_idx].name, h, nlen);
		pac->e[out_idx].name[nlen] = '\0';
		pac->e[out_idx].offset = (u32)(off + L5PAC_HDR_SIZE);
		pac->e[out_idx].size = f_len;
		pac->e[out_idx].alloc_size = a_len;

		out_idx++;
		off += a_len;
	}

	pac->n = out_idx;
	return ERR_OK;
}
