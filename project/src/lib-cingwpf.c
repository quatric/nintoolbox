// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// CiNG Wish Pack File (*.wpf) archive implementation; see lib-cingwpf.h.
//-----------------------------------------------------------------------------
#include "lib-cingwpf.h"
#include "lib-std.h"
#include <string.h>
#include <stdlib.h>

static inline u32 cingwpf_le32 (const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

bool IsCingWpf (const u8 *head, size_t head_size, u64 file_size)
{
	if (!head || head_size < CINGWPF_HDR_SIZE)
		return false;

	// Wiimms Patch File starts with "WPF\1" (0x57504601); do not conflict
	if (!memcmp (head, "WPF\x01", 4))
		return false;

	// First entry filename in bytes 0..23: null-terminated, printable ASCII
	const u8 ch0 = head[0];
	if (ch0 != 0x5c && ch0 != 0x2f && (ch0 < 0x20 || ch0 > 0x7e))
		return false;

	size_t name_len = 0;
	while (name_len < 24 && head[name_len] != 0)
	{
		const u8 ch = head[name_len];
		if (ch < 0x20 || ch > 0x7e)
			return false;
		name_len++;
	}

	if (name_len == 0 || name_len >= 24)
		return false;

	const u32 fsize = cingwpf_le32 (head + 24);
	const u32 next_off = cingwpf_le32 (head + 28);

	if (fsize == 0)
		return false;
	if (file_size && (fsize > file_size || next_off > file_size))
		return false;
	if (next_off < CINGWPF_HDR_SIZE + fsize)
		return false;

	// If a second entry exists within head_size, probe it
	if (next_off + CINGWPF_HDR_SIZE <= head_size)
	{
		const u8 *next = head + next_off;
		const u8 next_ch0 = next[0];
		if (next_ch0 != 0x5c && next_ch0 != 0x2f && (next_ch0 < 0x20 || next_ch0 > 0x7e))
			return false;

		size_t next_len = 0;
		while (next_len < 24 && next[next_len] != 0)
		{
			const u8 ch = next[next_len];
			if (ch < 0x20 || ch > 0x7e)
				return false;
			next_len++;
		}

		if (next_len == 0 || next_len >= 24)
			return false;

		const u32 fsize2 = cingwpf_le32 (next + 24);
		const u32 next_off2 = cingwpf_le32 (next + 28);

		if (fsize2 == 0)
			return false;
		if (file_size && (fsize2 > file_size || next_off2 > file_size))
			return false;
		if (next_off2 < next_off + CINGWPF_HDR_SIZE + fsize2)
			return false;
	}

	return true;
}

void CingWpfFree (cingwpf_t *wpf)
{
	if (!wpf)
		return;
	FREE (wpf->e);
	memset (wpf, 0, sizeof (*wpf));
}

enumError CingWpfParse (cingwpf_t *wpf, const u8 *d, size_t size, u64 file_size)
{
	if (!wpf || !d)
		return ERR_INVALID_DATA;
	memset (wpf, 0, sizeof (*wpf));

	if (!IsCingWpf (d, size, file_size))
		return ERR_INVALID_DATA;

	const u64 total_sz = file_size ? file_size : (u64)size;

	// Count entries
	uint total_entries = 0;
	u64 off = 0;
	while (off + CINGWPF_HDR_SIZE <= total_sz && off + CINGWPF_HDR_SIZE <= size)
	{
		const u8 *h = d + off;
		size_t nlen = 0;
		while (nlen < 24 && h[nlen] != 0)
			nlen++;
		if (nlen == 0)
			break;

		const u32 fsize = cingwpf_le32 (h + 24);
		const u32 next_off = cingwpf_le32 (h + 28);

		if (fsize == 0 || off + CINGWPF_HDR_SIZE + fsize > total_sz)
			break;

		total_entries++;
		if (next_off <= off)
			break;
		off = next_off;
	}

	if (!total_entries)
		return ERR_NOTHING_TO_DO;

	wpf->e = CALLOC (total_entries, sizeof (*wpf->e));
	if (!wpf->e)
		return ERR_OUT_OF_MEMORY;

	// Fill entries
	uint out_idx = 0;
	off = 0;
	while (off + CINGWPF_HDR_SIZE <= total_sz && off + CINGWPF_HDR_SIZE <= size && out_idx < total_entries)
	{
		const u8 *h = d + off;
		size_t nlen = 0;
		while (nlen < 24 && h[nlen] != 0)
			nlen++;
		if (nlen == 0)
			break;

		const u32 fsize = cingwpf_le32 (h + 24);
		const u32 next_off = cingwpf_le32 (h + 28);

		if (fsize == 0 || off + CINGWPF_HDR_SIZE + fsize > total_sz)
			break;

		// Strip leading backslash or slash for clean extraction
		const u8 *name_start = h;
		size_t clean_len = nlen;
		if (clean_len > 0 && (name_start[0] == '\\' || name_start[0] == '/'))
		{
			name_start++;
			clean_len--;
		}

		if (clean_len >= sizeof (wpf->e[out_idx].name))
			clean_len = sizeof (wpf->e[out_idx].name) - 1;

		memcpy (wpf->e[out_idx].name, name_start, clean_len);
		wpf->e[out_idx].name[clean_len] = '\0';
		wpf->e[out_idx].offset = (u32)(off + CINGWPF_HDR_SIZE);
		wpf->e[out_idx].size = fsize;
		wpf->e[out_idx].next_offset = next_off;

		out_idx++;
		if (next_off <= off)
			break;
		off = next_off;
	}

	wpf->n = out_idx;
	return ERR_OK;
}
