// SPDX-License-Identifier: GPL-2.0+
// "Octomania" (Wii) formats -- see lib-octomania.h for exactly what is and
// is not understood about each of them.

#include "lib-octomania.h"
#include "lib-nintendo.h"
#include <string.h>
#include <ctype.h>

//-----------------------------------------------------------------------------
// shared helpers

static void print_escaped (FILE *f, const u8 *data, size_t len)
{
	for (size_t i = 0; i < len; i++)
	{
		u8 c = data[i];
		if (!c)
			break;
		if (c == '\\')
			fputs ("\\\\", f);
		else if (c >= 0x20 && c < 0x7f)
			fputc (c, f);
		else
			fprintf (f, "\\x%02x", c);
	}
}

//-----------------------------------------------------------------------------
// (1) ".sec" "secB" scene/demo container

enum { OCTO_SEC_HEADER_SIZE = 0x20, OCTO_SEC_ENTRY_SIZE = 0x20 };

int IsOctomaniaSec (const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!data || size < OCTO_SEC_HEADER_SIZE)
		return 0;
	if (memcmp (data, "secB", 4))
		return 0;
	return rd_be32 (data + 4) == 1;
}

enumError DecodeOctomaniaSec_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsOctomaniaSec (data, size, file_size))
		return EINVAL;

	u32 block_size      = rd_be32 (data + 0x08);
	u32 string_tab_ofs  = rd_be32 (data + 0x0c);
	u32 block_size2     = rd_be32 (data + 0x10);
	u32 count           = rd_be32 (data + 0x14);
	u32 group_count     = rd_be32 (data + 0x18);
	u32 leaf_count      = rd_be32 (data + 0x1c);

	fprintf (f, "# Octomania secB scene/demo container (.sec)\n");
	fprintf (f, "block_size = 0x%x  # big-endian, constant 0x800 in every sample seen\n", block_size);
	fprintf (f, "string_table_offset = 0x%x  # big-endian\n", string_tab_ofs);
	fprintf (f, "block_size2 = 0x%x  # big-endian, same as block_size in every sample seen\n", block_size2);
	fprintf (f, "chunk_count = %u  # big-endian, number of 32-byte chunk-table entries\n", count);
	fprintf (f, "group_count = %u  # big-endian\n", group_count);
	fprintf (f, "leaf_count = %u  # big-endian\n", leaf_count);

	if ((u64) OCTO_SEC_HEADER_SIZE + (u64) count * OCTO_SEC_ENTRY_SIZE > size)
	{
		fprintf (f, "# chunk table truncated/out-of-bounds -- not decoded further\n");
		return ERR_OK;
	}

	fprintf (f, "\n# chunk table (%u entries, 32 bytes each, starting at 0x%x)\n",
		count, OCTO_SEC_HEADER_SIZE);

	for (u32 i = 0; i < count; i++)
	{
		const u8 *e = data + OCTO_SEC_HEADER_SIZE + (size_t) i * OCTO_SEC_ENTRY_SIZE;
		u32 f0 = rd_be32 (e + 0x00);
		u32 f1 = rd_be32 (e + 0x04);
		u32 f2 = rd_be32 (e + 0x08);
		u32 f3 = rd_be32 (e + 0x0c);
		u32 f4 = rd_be32 (e + 0x10);
		u32 f5 = rd_be32 (e + 0x14);
		u32 f6 = rd_be32 (e + 0x18);
		u32 f7 = rd_be32 (e + 0x1c);

		fprintf (f, "chunk[%u]: field0=%u index=%u tag=%u name_ofs_plus1=%u"
			" field4=%u field5=%u field6=%u field7=%u\n",
			i, f0, f1, f2, f3, f4, f5, f6, f7);

		if (i == 0)
		{
			fprintf (f, "  # group descriptor -- field0 is the leaf/entry count\n");
			continue;
		}
		if (i + 1 == count)
		{
			fprintf (f, "  # data trailer -- field7 looks like an overall payload byte size\n");
			continue;
		}
		if (f3 >= 1 && (size_t) string_tab_ofs + (f3 - 1) < size)
		{
			size_t start = (size_t) string_tab_ofs + (f3 - 1);
			size_t j = start;
			while (j < size && data[j])
				j++;
			if (j > start && j < size)
			{
				fprintf (f, "  name = \"");
				print_escaped (f, data + start, j - start);
				fprintf (f, "\"\n");
			}
		}
	}

	fprintf (f, "# per-chunk binary payload data not reverse-engineered -- see lib-octomania.h\n");
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (2) ".wt" wavetable sample-offset index

int IsOctomaniaWt (const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!data || size < 8)
		return 0;

	// Magic-less: probe for a plausible leading table of strictly
	// increasing big-endian u32 byte offsets, terminated (or immediately
	// followed) by a 0xffffffff sentinel run -- confirmed shape of the
	// one real sample pulled from the disc (gm16adpcm.wt). Require at
	// least two valid increasing offsets before the first sentinel to
	// avoid false-positiving on arbitrary binary data.
	u32 prev = 0;
	int valid = 0;
	size_t off = 0;
	for (; off + 4 <= size && off < 0x1000; off += 4)
	{
		u32 v = rd_be32 (data + off);
		if (v == 0xffffffff)
			break;
		if (v < prev)
			return 0;
		prev = v;
		valid++;
	}
	return valid >= 2 && off + 4 <= size && rd_be32 (data + off) == 0xffffffff;
}

enumError DecodeOctomaniaWt_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsOctomaniaWt (data, size, file_size))
		return EINVAL;

	fprintf (f, "# Octomania wavetable sample-offset index (.wt)\n");
	fprintf (f, "# leading table of big-endian sample byte-offsets into the companion .pcm file\n");

	u32 index = 0;
	for (size_t off = 0; off + 4 <= size; off += 4, index++)
	{
		u32 v = rd_be32 (data + off);
		if (v == 0xffffffff)
			break;
		fprintf (f, "sample_offset[%u] = 0x%x\n", index, v);
	}

	fprintf (f, "# MIDI-program index map and per-entry metadata following the offset table\n");
	fprintf (f, "# not reverse-engineered -- see lib-octomania.h\n");
	return ERR_OK;
}
