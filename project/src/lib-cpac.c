// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Capcom CPAC multi-section archive container implementation; see lib-cpac.h.
//-----------------------------------------------------------------------------
#include "lib-cpac.h"
#include "lib-std.h"
#include <string.h>
#include <stdlib.h>

#define CPAC_TAG_BKEY 0x424b4559 // "YEKB" in LE
#define CPAC_TAG_PKEY 0x504b4559 // "YEKP" in LE
#define CPAC_TAG_BDAT 0x42444154 // "TADB" in LE
#define CPAC_TAG_PDAT 0x50444154 // "TADP" in LE

static inline u32 cpac_le32 (const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static inline u16 cpac_le16 (const u8 *p)
{
	return (u16)p[0] | ((u16)p[1] << 8);
}

bool IsCpac (const u8 *head, size_t head_size, u64 file_size)
{
	if (!head || head_size < 0x20)
		return false;

	const u32 head_len = cpac_le32 (head);
	if (head_len < 0x20 || (head_len % 8) != 0 || head_len > 0x100)
		return false;
	if (file_size && head_len >= file_size)
		return false;
	if (head_size < head_len + 24)
		return false;

	const u8 *s0 = head + head_len;
	const u32 tag_hdr_len = cpac_le32 (s0);
	const u32 version = cpac_le32 (s0 + 4);
	const u32 key_tag = cpac_le32 (s0 + 8);
	const u32 next_off = cpac_le32 (s0 + 12);
	const u32 dat_tag = cpac_le32 (s0 + 16);

	if (tag_hdr_len != 24 || version != 2 || next_off != 24)
		return false;
	if (key_tag != CPAC_TAG_BKEY && key_tag != CPAC_TAG_PKEY)
		return false;
	if (dat_tag != CPAC_TAG_BDAT && dat_tag != CPAC_TAG_PDAT)
		return false;

	return true;
}

size_t CpacHeadSize (const u8 *head, size_t size)
{
	if (!IsCpac (head, size, 0))
		return 0;

	return cpac_le32 (head);
}

void CpacFree (cpac_t *cpac)
{
	if (!cpac)
		return;
	FREE (cpac->e);
	memset (cpac, 0, sizeof (*cpac));
}

enumError CpacParse (cpac_t *cpac, const u8 *d, size_t size, u64 file_size)
{
	if (!cpac || !d)
		return ERR_INVALID_DATA;
	memset (cpac, 0, sizeof (*cpac));

	if (!IsCpac (d, size, file_size))
		return ERR_INVALID_DATA;

	const u64 available = file_size && file_size < size ? file_size : size;
	const u32 head_len = cpac_le32 (d);
	const uint n_sections = head_len / 8;
	if (!n_sections)
		return ERR_INVALID_DATA;

	// Calculate section offsets and sizes
	typedef struct sec_info_t {
		u32 offset;
		u32 size;
	} sec_info_t;

	sec_info_t *secs = CALLOC (n_sections, sizeof (*secs));
	if (!secs)
		return ERR_OUT_OF_MEMORY;

	secs[0].offset = head_len;
	secs[0].size = cpac_le32 (d + 4);

	for (uint i = 1; i < n_sections; i++)
	{
		secs[i].offset = cpac_le32 (d + i * 8);
		secs[i].size = cpac_le32 (d + i * 8 + 4);
	}

	// First pass: count entries
	uint total_entries = 0;
	for (uint i = 0; i < n_sections; i++)
	{
		const u32 soff = secs[i].offset;
		const u32 ssz = secs[i].size;
		if (ssz < 24 || (u64)soff + ssz > available)
			continue;

		const u8 *shead = d + soff;
		const u32 key_tag = cpac_le32 (shead + 8);
		const u32 tbl_len = cpac_le32 (shead + 20);
		if (tbl_len > ssz)
			continue;

		if (key_tag == CPAC_TAG_BKEY && tbl_len >= 32)
		{
			const uint n_recs = (tbl_len - 32) / 16;
			if ((u64)soff + tbl_len <= available)
			{
				for (uint r = 0; r < n_recs; r++)
				{
					const u32 s1 = cpac_le32 (shead + 32 + r * 16 + 4) & 0x7FFFFFFF;
					const u32 s2 = cpac_le32 (shead + 32 + r * 16 + 12) & 0x7FFFFFFF;
					if (s1 > 0) total_entries++;
					if (s2 > 0) total_entries++;
				}
			}
		}
		else if (key_tag == CPAC_TAG_PKEY && tbl_len >= 32)
		{
			const uint n_desc = (tbl_len - 32) / 4;
			total_entries += n_desc;
		}
	}

	if (!total_entries)
	{
		FREE (secs);
		return ERR_NOTHING_TO_DO;
	}

	cpac->e = CALLOC (total_entries, sizeof (*cpac->e));
	if (!cpac->e)
	{
		FREE (secs);
		return ERR_OUT_OF_MEMORY;
	}

	// Second pass: fill entries
	uint out_idx = 0;
	for (uint i = 0; i < n_sections; i++)
	{
		const u32 soff = secs[i].offset;
		const u32 ssz = secs[i].size;
		if (ssz < 24 || (u64)soff + ssz > available)
			continue;

		const u8 *shead = d + soff;
		const u32 key_tag = cpac_le32 (shead + 8);
		const u32 tbl_len = cpac_le32 (shead + 20);
		if (tbl_len > ssz)
			continue;
		const u64 payload_start = (u64)soff + tbl_len;

		if (key_tag == CPAC_TAG_BKEY && tbl_len >= 32 && (u64)soff + tbl_len <= available)
		{
			const uint n_recs = (tbl_len - 32) / 16;
			uint sec_file = 0;
			for (uint r = 0; r < n_recs; r++)
			{
				const u32 o1_raw = cpac_le32 (shead + 32 + r * 16);
				const u32 s1_raw = cpac_le32 (shead + 32 + r * 16 + 4);
				const u32 o2_raw = cpac_le32 (shead + 32 + r * 16 + 8);
				const u32 s2_raw = cpac_le32 (shead + 32 + r * 16 + 12);

				const u32 parts[2][2] = { { o1_raw, s1_raw }, { o2_raw, s2_raw } };
				for (int p = 0; p < 2; p++)
				{
					const u32 raw_o = parts[p][0];
					const u32 raw_s = parts[p][1];
					const u32 sz = raw_s & 0x7FFFFFFF;
					if (sz > 0 && out_idx < total_entries)
					{
						const u64 off = payload_start + (raw_o & 0x7FFFFFFF);
						if (off <= 0xffffffffULL && off + sz <= (u64)soff + ssz)
						{
							cpac->e[out_idx].offset = off;
							cpac->e[out_idx].size = sz;
							cpac->e[out_idx].section = (u16)i;
							cpac->e[out_idx].subindex = (u16)(sec_file++);
							cpac->e[out_idx].is_palette = false;
							cpac->e[out_idx].is_lz11 = (raw_s & 0x80000000) != 0;
							out_idx++;
						}
					}
				}
			}
		}
		else if (key_tag == CPAC_TAG_PKEY && tbl_len >= 32 && (u64)soff + tbl_len <= available)
		{
			const uint n_desc = (tbl_len - 32) / 4;
			for (uint d_idx = 0; d_idx < n_desc; d_idx++)
			{
				if (out_idx >= total_entries)
					break;
				const u32 desc = cpac_le32 (shead + 32 + d_idx * 4);
				const u16 w0 = desc & 0xFFFF;
				const u16 w1 = (desc >> 16) & 0xFFFF;
				const u32 pal_sz = (w0 == 0x0100) ? 512 : 32;
				const u64 off = payload_start + (u32)w1 * 32;

				if (off <= 0xffffffffULL && off + pal_sz <= (u64)soff + ssz)
				{
					cpac->e[out_idx].offset = off;
					cpac->e[out_idx].size = pal_sz;
					cpac->e[out_idx].section = (u16)i;
					cpac->e[out_idx].subindex = (u16)d_idx;
					cpac->e[out_idx].is_palette = true;
					cpac->e[out_idx].is_lz11 = false;
					out_idx++;
				}
			}
		}
	}

	FREE (secs);
	cpac->n = out_idx;
	return ERR_OK;
}
