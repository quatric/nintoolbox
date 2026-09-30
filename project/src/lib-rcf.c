// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Radical "ATG CORE CEMENT LIBRARY" parser; see lib-rcf.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-rcf.h"
#include "lib-std.h"
#include <string.h>
#include <stdlib.h>

#define RCF_MAGIC "ATG CORE CEMENT LIBRARY"
#define RCF_HEAD 0x3c
#define RCF_ENTRY 12
#define RCF_MAX_ENTRIES 0x100000u
#define RCF_MAX_HEAD 0x8000000u // 128 MiB of table + names is far beyond real files

static u32 rc_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
static u32 rc_le32 (const u8 *p)
{
	return (u32)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0];
}

size_t RcfHeadSize (const u8 *head, size_t size)
{
	if (!head || size < RCF_HEAD || strncmp ((const char *)head, RCF_MAGIC, sizeof (RCF_MAGIC) - 1))
		return 0;
	if (head[0x20] != 2)
		return 0;
	const u64 names_off = rc_be32 (head + 0x2c), names_len = rc_be32 (head + 0x30);
	const u32 count = rc_be32 (head + 0x38);
	if (!count || count > RCF_MAX_ENTRIES || rc_be32 (head + 0x24) != RCF_HEAD)
		return 0;
	if (RCF_HEAD + (u64)count * RCF_ENTRY > names_off)
		return 0;
	const u64 total = names_off + names_len;
	return total > RCF_MAX_HEAD ? 0 : (size_t)total;
}

void RcfFree (rcf_t *r)
{
	if (!r)
		return;
	for (uint i = 0; i < r->n; i++)
		FREE (r->e[i].name);
	FREE (r->e);
	memset (r, 0, sizeof (*r));
}

static int rc_cmp_off (const void *a, const void *b)
{
	const u32 x = ((const rcf_entry_t *)a)->offset, y = ((const rcf_entry_t *)b)->offset;
	return x < y ? -1 : x > y;
}

enumError RcfParse (rcf_t *r, const u8 *d, size_t size, u64 file_size)
{
	memset (r, 0, sizeof (*r));
	const size_t need = RcfHeadSize (d, size);
	if (!need || need > size)
		return ERR_INVALID_DATA;

	const u32 count = rc_be32 (d + 0x38);
	const u32 names_off = rc_be32 (d + 0x2c), names_len = rc_be32 (d + 0x30);

	r->e = CALLOC (count, sizeof (*r->e));
	if (!r->e)
		return ERR_OUT_OF_MEMORY;
	r->n = count;
	for (uint i = 0; i < count; i++)
	{
		const u8 *p = d + RCF_HEAD + (size_t)i * RCF_ENTRY;
		r->e[i].hash = rc_be32 (p);
		r->e[i].offset = rc_be32 (p + 4);
		r->e[i].size = rc_be32 (p + 8);
		if ((u64)r->e[i].offset + r->e[i].size > file_size)
		{
			RcfFree (r);
			return ERR_INVALID_DATA;
		}
	}
	qsort (r->e, count, sizeof (*r->e), rc_cmp_off);

	// names, in ascending offset order
	const u8 *blk = d + names_off;
	size_t o = 8;
	if (names_len < 8 || rc_le32 (blk) != 0x800)
		return ERR_OK; // valid table, unusable names: caller falls back to hashes
	for (uint i = 0; i < count; i++)
	{
		if (o + 16 > names_len)
			break;
		const u32 ts = rc_le32 (blk + o), a = rc_le32 (blk + o + 4), b = rc_le32 (blk + o + 8),
				  len = rc_le32 (blk + o + 12);
		if (a != 0x800 || b || !len || len > 1024 || o + 16 + len > names_len)
			break;
		char *nm = MALLOC ((size_t)len + 1);
		if (!nm)
			break;
		memcpy (nm, blk + o + 16, len);
		nm[len] = 0;
		for (char *c = nm; *c; c++)
			if (*c == '\\')
				*c = '/';
		r->e[i].name = nm;
		r->e[i].mtime = ts;
		o += 16 + len + 3;
	}
	return ERR_OK;
}
