// SPDX-License-Identifier: GPL-2.0+
// "The Dog Island" (Wii) formats -- see lib-dogisland.h for exactly what
// is and is not understood about each of them.

#include "lib-dogisland.h"
#include "lib-nintendo.h"
#include <stdio.h>
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

// Same escaping, but a byte span may legitimately contain more than one
// NUL-terminated sub-field (e.g. a ".wds" entry span holds a short tag+name
// field followed by NUL padding and then a separate dialogue-text field) --
// so this prints every non-empty NUL-delimited chunk in the span, joined by
// " | ", instead of stopping at the first NUL.
static void print_escaped_multi (FILE *f, const u8 *data, size_t len)
{
	int first = 1;
	size_t i = 0;
	while (i < len)
	{
		while (i < len && !data[i])
			i++;
		size_t start = i;
		while (i < len && data[i])
			i++;
		if (i > start)
		{
			if (!first)
				fputs (" | ", f);
			print_escaped (f, data + start, i - start);
			first = 0;
		}
	}
}

//-----------------------------------------------------------------------------
// (1) ".wds" WARDP dialogue string table

#define WDS_MAX_COUNT 100000

int IsDogIslandWds (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < 12 || memcmp (data, "WARD", 4))
		return 0;
	if (data[5] || data[6] || data[7])
		return 0;

	u32 count = rd_le32 (data + 8);
	if (!count || count > WDS_MAX_COUNT)
		return 0;

	uint n = 2 * count - 1;
	size_t arr_end = 12 + (size_t)n * 4;
	if (arr_end > size)
		return size < file_size; // short probe buffer: plausible header only

	// Sanity-check the interleaved counter slots (index 1,3,5,... of the
	// array) really are the confirmed 0,1,2,...,count-2 sequence.
	for (u32 i = 0; i < count - 1; i++)
	{
		u32 idx_val = rd_le32 (data + 12 + (2 * i + 1) * 4);
		if (idx_val != i)
			return 0;
	}

	// Offsets must be non-decreasing.
	u32 prev = 0;
	for (u32 i = 0; i < count; i++)
	{
		u32 off = rd_le32 (data + 12 + 2 * i * 4);
		if (i && off < prev)
			return 0;
		prev = off;
	}

	size_t base = (arr_end + 15) & ~(size_t)15;
	return base <= file_size;
}

enumError DecodeDogIslandWds_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsDogIslandWds (data, size, file_size))
		return EINVAL;

	u32 count = rd_le32 (data + 8);
	uint n = 2 * count - 1;
	size_t arr_end = 12 + (size_t)n * 4;
	size_t base = (arr_end + 15) & ~(size_t)15;

	fprintf (f, "# The Dog Island WARDP dialogue table\n");
	fprintf (f, "# variant byte: 0x%02x (meaning not understood)\n", data[4]);
	fprintf (f, "# %u string entries, data base 0x%zx\n\n", count, base);

	for (u32 i = 0; i < count; i++)
	{
		u32 off = rd_le32 (data + 12 + 2 * i * 4);
		size_t pos = ((u64)base + off <= size) ? (size_t)(base + off) : size;
		size_t next_pos = size;
		if (i + 1 < count)
		{
			u32 next_off = rd_le32 (data + 12 + 2 * (i + 1) * 4);
			next_pos = ((u64)base + next_off <= size) ? (size_t)(base + next_off) : size;
		}

		fprintf (f, "[%u] offset=0x%x: \"", i, off);
		if (pos < next_pos)
			print_escaped_multi (f, data + pos, next_pos - pos);
		fprintf (f, "\"\n");
	}

	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (2) ".wdb" offset/string table

#define WDB_RECORD_SIZE 24
#define WDB_MAX_COUNT   100000

// NOTE: only the record's first dword ("start", a byte offset into the
// trailing string blob) was confirmed to be stable across every real
// sample -- see lib-dogisland.h. Earlier samples looked like the
// remaining 5 dwords were duplicate start/end fields plus a flags word,
// but a wider sample pull turned up files where that specific sub-layout
// does not hold (the values are still small structured integers in the
// same ballpark, just not in the exact same relative positions), so this
// decoder does not assert anything about dwords 1..5 beyond reporting
// them raw. Only "start" is used to slice the string blob (paired with
// the next record's start, or end-of-file for the last record, exactly
// like the ".wds" decoder above).
int IsDogIslandWdb (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < 16)
		return 0;

	u32 count = rd_le32 (data);
	u32 header_size = rd_le32 (data + 4);
	if (header_size != 16)
		return 0;
	if (!count || count > WDB_MAX_COUNT)
		return 0;

	size_t table_size = 16 + (size_t)count * WDB_RECORD_SIZE;
	if (table_size > size)
		return size < file_size;

	// record[0].start must equal the table size exactly (confirmed rule).
	u32 start0 = rd_le32 (data + 16);
	if (start0 != table_size)
		return 0;

	// Starts must be non-decreasing and land inside the file.
	u32 prev = start0;
	for (u32 i = 1; i < count; i++)
	{
		u32 start = rd_le32 (data + 16 + (size_t)i * WDB_RECORD_SIZE);
		if (start < prev || start > file_size)
			return 0;
		prev = start;
	}

	return table_size <= file_size;
}

enumError DecodeDogIslandWdb_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsDogIslandWdb (data, size, file_size))
		return EINVAL;

	u32 count = rd_le32 (data);
	size_t table_size = 16 + (size_t)count * WDB_RECORD_SIZE;

	fprintf (f, "# The Dog Island .wdb offset/string table\n");
	fprintf (f, "# %u entries, string blob base 0x%zx\n", count, table_size);
	fprintf (f, "# only the 'start' offset (dword 0) is confirmed; dwords 1..5\n");
	fprintf (f, "# are reported raw, their exact roles are not understood\n\n");

	for (u32 i = 0; i < count; i++)
	{
		const u8 *r = data + 16 + (size_t)i * WDB_RECORD_SIZE;
		u32 start = rd_le32 (r);
		size_t next = (i + 1 < count) ? rd_le32 (r + WDB_RECORD_SIZE) : size;
		if (start > size)
			start = (u32)size;
		if (next > size)
			next = size;

		fprintf (f, "[%u] start=0x%x raw=%08x,%08x,%08x,%08x,%08x: \"", i, start,
			rd_le32 (r + 4), rd_le32 (r + 8), rd_le32 (r + 12), rd_le32 (r + 16), rd_le32 (r + 20));
		if (start < next)
			print_escaped_multi (f, data + start, next - start);
		fprintf (f, "\"\n");
	}

	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (3) ".ymg"/".ymm" YOBJ model, bare or DUMY-wrapped

static int yobj_header_at (const u8 *data, size_t size, size_t file_size,
	size_t off, u32 *out_size, int *out_be)
{
	if (off + 8 > size)
		return 0;
	if (memcmp (data + off, "YOBJ", 4))
		return 0;

	u32 be = rd_be32 (data + off + 4);
	u32 le = rd_le32 (data + off + 4);
	// Accept whichever endianness puts (off + 8 + size) within a sane
	// range of the real file size.
	if (off + 8 + (size_t)be <= file_size + 16 && be > 0)
	{
		*out_size = be;
		*out_be = 1;
		return 1;
	}
	if (off + 8 + (size_t)le <= file_size + 16 && le > 0)
	{
		*out_size = le;
		*out_be = 0;
		return 1;
	}
	return 0;
}

int IsDogIslandYobj (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < 8)
		return 0;

	u32 dummy_size;
	int dummy_be;

	// Bare "YOBJ" at offset 0.
	if (yobj_header_at (data, size, file_size, 0, &dummy_size, &dummy_be))
		return 1;

	// DUMY-wrapped: "DUMY" + BE u32 header_size(==16, describing the 16
	// reserved bytes that follow -- so the inner chunk starts at byte 24,
	// not 16) + 16 zero bytes, then the inner "YOBJ" chunk at offset 24.
	if (size >= 24 && !memcmp (data, "DUMY", 4))
	{
		u32 header_size = rd_be32 (data + 4);
		if (header_size == 16)
			return yobj_header_at (data, size, file_size, 24, &dummy_size, &dummy_be);
	}

	return 0;
}

enumError DecodeDogIslandYobj_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsDogIslandYobj (data, size, file_size))
		return EINVAL;

	fprintf (f, "# The Dog Island YOBJ model\n");

	size_t off = 0;
	if (!memcmp (data, "DUMY", 4))
	{
		fprintf (f, "# DUMY wrapper, header_size=%u\n", rd_be32 (data + 4));
		off = 24;
	}

	u32 inner_size;
	int inner_be;
	if (yobj_header_at (data, size, file_size, off, &inner_size, &inner_be))
	{
		fprintf (f, "# YOBJ chunk at 0x%zx, size=0x%x (%s)\n", off, inner_size,
			inner_be ? "big-endian size field" : "little-endian size field");
	}

	fprintf (f, "# internal bone/mesh table not reverse-engineered -- see lib-dogisland.h\n");
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (4) ".pms" EVNT event script (header only)

int IsDogIslandPms (const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!data || size < 16 || memcmp (data, "EVNT", 4))
		return 0;
	for (int i = 4; i < 12; i++)
		if (data[i])
			return 0;
	return 1;
}

enumError DecodeDogIslandPms_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsDogIslandPms (data, size, file_size))
		return EINVAL;

	u32 event_id = rd_le32 (data + 12);
	fprintf (f, "# The Dog Island EVNT event script\n");
	fprintf (f, "event_id = %u (0x%x)\n", event_id, event_id);
	if (size >= 20)
		fprintf (f, "field_0x10 = %u (0x%x)\n", rd_le32 (data + 16), rd_le32 (data + 16));
	fprintf (f, "# script bytecode body (offset 0x14+) not reverse-engineered\n");
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (5) ".mtt"/".ypc"/".pac" DUMY+POF0 container

int IsDogIslandMtt (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < 32 || memcmp (data, "DUMY", 4))
		return 0;
	// header_size describes the 16 reserved bytes that follow it, so the
	// inner chunk starts at byte 24 (4 magic + 4 size + 16 reserved), not
	// byte 16 -- confirmed against real samples (a naive off-by-8 guess
	// at byte 16 lands mid-way through the reserved zero run instead).
	if (rd_be32 (data + 4) != 16)
		return 0;
	for (int i = 8; i < 24; i++)
		if (data[i])
			return 0;
	if (memcmp (data + 24, "POF0", 4))
		return 0;

	u32 inner_size = rd_be32 (data + 28);
	if (!inner_size)
		return 0;
	return 24 + 8 + (size_t)inner_size <= file_size + 16;
}

enumError DecodeDogIslandMtt_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsDogIslandMtt (data, size, file_size))
		return EINVAL;

	u32 inner_size = rd_be32 (data + 28);
	fprintf (f, "# The Dog Island DUMY+POF0 pointer-fixup container\n");
	fprintf (f, "inner_chunk = POF0\n");
	fprintf (f, "inner_size = %u (0x%x)  # big-endian field\n", inner_size, inner_size);
	fprintf (f, "# packed pointer table body not reverse-engineered -- see lib-dogisland.h\n");
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (6) ".cprm" fixed 64-byte-record BE float table

#define CPRM_RECORD_SIZE 64
#define CPRM_MAX_COUNT   100000

int IsDogIslandCprm (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < 16)
		return 0;

	u32 count = rd_be32 (data);
	u32 header_size = rd_be32 (data + 4);
	if (header_size != 16)
		return 0;
	if (count > CPRM_MAX_COUNT)
		return 0;
	for (int i = 8; i < 16; i++)
		if (data[i])
			return 0;

	size_t total = 16 + (size_t)count * CPRM_RECORD_SIZE;
	return total == file_size;
}

enumError DecodeDogIslandCprm_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsDogIslandCprm (data, size, file_size))
		return EINVAL;

	u32 count = rd_be32 (data);
	if ((u64)16 + (u64)count * CPRM_RECORD_SIZE > size)
		return EINVAL;
	fprintf (f, "# The Dog Island .cprm fixed-record table (big-endian)\n");
	fprintf (f, "# %u records of 64 bytes each; per-field meaning not understood\n\n", count);

	for (u32 i = 0; i < count; i++)
	{
		const u8 *r = data + 16 + (size_t)i * CPRM_RECORD_SIZE;
		fprintf (f, "[%u]", i);
		for (int w = 0; w < 16; w++)
		{
			u32 raw = rd_be32 (r + w * 4);
			float fval;
			memcpy (&fval, &raw, 4);
			fprintf (f, " %g", (double) fval);
		}
		fprintf (f, "\n");
	}

	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (7) ".efi"/".sci"/".qci" script bytecode (structural probe only)

int IsDogIslandScript (const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!data || size < 16)
		return 0;

	u32 header_size = rd_le32 (data + 8);
	u32 zero = rd_le32 (data + 12);
	// Confirmed across samples: a small header_size (0x10 or 0x18) and a
	// zero word right after it. Loose on purpose -- callers must gate
	// this on extension, see lib-dogisland.h.
	if (zero != 0)
		return 0;
	if (header_size != 0x10 && header_size != 0x18)
		return 0;
	return 1;
}

enumError DecodeDogIslandScript_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsDogIslandScript (data, size, file_size))
		return EINVAL;

	fprintf (f, "# The Dog Island compiled script bytecode\n");
	fprintf (f, "field_a = 0x%x\n", rd_le32 (data));
	fprintf (f, "field_b = 0x%x\n", rd_le32 (data + 4));
	fprintf (f, "header_size = 0x%x\n", rd_le32 (data + 8));
	fprintf (f, "# opcode bytecode body not reverse-engineered -- see lib-dogisland.h\n");
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (8) ".mpq" custom (non-Blizzard) container

int IsDogIslandMpq (const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!data || size < 20)
		return 0;
	// Blizzard MPQ magic is "MPQ\x1A"; this format uses "MPQ\0" -- the
	// fourth byte is the discriminator confirmed against both samples on
	// this disc.
	if (memcmp (data, "MPQ\0", 4))
		return 0;
	return 1;
}

enumError DecodeDogIslandMpq_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsDogIslandMpq (data, size, file_size))
		return EINVAL;

	fprintf (f, "# The Dog Island .mpq container (NOT Blizzard MPQ -- magic is\n");
	fprintf (f, "# \"MPQ\\0\", not Blizzard's \"MPQ\\x1A\", and the header layout does\n");
	fprintf (f, "# not match Blizzard's either; verified empirically, see lib-dogisland.h)\n");
	fprintf (f, "field_a = 0x%x  # big-endian field\n", rd_be32 (data + 4));
	fprintf (f, "field_b = 0x%x  # big-endian field\n", rd_be32 (data + 8));
	fprintf (f, "field_c = 0x%x  # big-endian field\n", rd_be32 (data + 12));
	fprintf (f, "field_d = 0x%x  # big-endian field\n", rd_be32 (data + 16));
	fprintf (f, "# internal archive table not reverse-engineered -- see lib-dogisland.h\n");
	return ERR_OK;
}
