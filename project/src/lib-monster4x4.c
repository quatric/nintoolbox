// SPDX-License-Identifier: GPL-2.0+
// Monster 4x4: Stunt Racer "CHNK" chunked container -- see lib-monster4x4.h
// for exactly what is and is not understood about this format.

#include "lib-monster4x4.h"
#include "lib-nintendo.h"
#include <stdio.h>
#include <string.h>

#define CHNK_HEADER_SIZE  16
#define CHNK_ENTRY_SIZE   16
#define CHNK_MAX_CHUNKS   4096 // sanity cap; real files use single digits to a few dozen

static int chnk_tag_ok (const u8 *tag)
{
	for (int i = 0; i < 4; i++)
	{
		u8 c = tag[i];
		int ok = c >= 'A' && c <= 'Z' || c >= '0' && c <= '9' || c == '_';
		if (!ok)
			return 0;
	}
	return 1;
}

int IsCHNK (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < CHNK_HEADER_SIZE || memcmp (data, "CHNK", 4))
		return 0;

	u32 total = rd_le32 (data + 4);
	u32 count = rd_le32 (data + 8);

	// The strongest, near-impossible-by-chance signal: the header's own
	// size field matches the file's real size exactly.
	if (total != file_size)
		return 0;
	if (!count || count > CHNK_MAX_CHUNKS)
		return 0;
	if ((u64)CHNK_HEADER_SIZE + (u64)count * CHNK_ENTRY_SIZE > total)
		return 0;

	// Validate every table entry we can actually see (a FILETYPE probe may
	// only hand over a short prefix, so don't require the whole table).
	size_t avail_entries = size >= CHNK_HEADER_SIZE
		? (size - CHNK_HEADER_SIZE) / CHNK_ENTRY_SIZE : 0;
	if (avail_entries > count)
		avail_entries = count;
	if (!avail_entries)
		return size < file_size; // truncated probe too short to see even one entry

	for (size_t i = 0; i < avail_entries; i++)
	{
		const u8 *e = data + CHNK_HEADER_SIZE + i * CHNK_ENTRY_SIZE;
		if (!chnk_tag_ok (e))
			return 0;
		u32 off  = rd_le32 (e + 4);
		u32 dsz  = rd_le32 (e + 12);
		if ((u64)off + (u64)dsz > total)
			return 0;
	}
	return 1;
}

enumError DecodeCHNK_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f)
		return EINVAL;
	if (!IsCHNK (data, size, file_size))
		return EINVAL;

	u32 total = rd_le32 (data + 4);
	u32 count = rd_le32 (data + 8);
	u32 version = rd_le32 (data + 12);

	fprintf (f, "# Monster 4x4: Stunt Racer CHNK container\n");
	fprintf (f, "# top-level chunk table only; chunk payloads are located but not decoded\n");
	fprintf (f, "total-size  = %u\n", total);
	fprintf (f, "chunk-count = %u\n", count);
	fprintf (f, "version     = %u\n", version);
	fprintf (f, "\n");
	fprintf (f, "# idx  tag   offset     size       field_b field_c\n");

	size_t avail_entries = size >= CHNK_HEADER_SIZE
		? (size - CHNK_HEADER_SIZE) / CHNK_ENTRY_SIZE : 0;
	if (avail_entries > count)
		avail_entries = count;

	for (size_t i = 0; i < avail_entries; i++)
	{
		const u8 *e = data + CHNK_HEADER_SIZE + i * CHNK_ENTRY_SIZE;
		char tag[5];
		memcpy (tag, e, 4);
		tag[4] = 0;
		u32 off = rd_le32 (e + 4);
		u16 fb  = rd_le16 (e + 8);
		u16 fc  = rd_le16 (e + 10);
		u32 dsz = rd_le32 (e + 12);
		fprintf (f, "%4zu  %-4s  0x%08x 0x%08x %6u  %6u\n",
			i, tag, off, dsz, fb, fc);
	}
	if (avail_entries < count)
		fprintf (f, "# ... %u more entries not shown (truncated input)\n",
			count - (u32)avail_entries);

	return ERR_OK;
}
