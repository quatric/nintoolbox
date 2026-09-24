// SPDX-License-Identifier: GPL-2.0+
// Red Steel 2 (Wii) formats -- see lib-redsteel2.h for exactly what is and
// is not understood about each.

#include "lib-redsteel2.h"
#include "lib-nintendo.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

//-----------------------------------------------------------------------------
// (1) ".bbf"/".BF" Ubisoft "ABE" bigfile

#define ABE_HEADER_SIZE   64
#define ABE_MAX_ENTRIES   1000000 // sanity cap

int IsRedSteel2Abe (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < ABE_HEADER_SIZE || memcmp (data, "ABE\0", 4))
		return 0;

	u32 version      = rd_le32 (data + 4);
	u32 entry_count  = rd_le32 (data + 8);
	u32 table_offset = rd_le32 (data + 32);
	u32 s0 = rd_le32 (data + 52);
	u32 s1 = rd_le32 (data + 56);
	u32 s2 = rd_le32 (data + 60);

	// Confirmed constant across every sample seen; gate on it loosely
	// (only version 4 seen) to avoid rejecting a real but different
	// version of the same family outright while still avoiding false
	// positives on unrelated data.
	if (version != 4)
		return 0;
	if (!entry_count || entry_count > ABE_MAX_ENTRIES)
		return 0;
	// The trailing three header words were 0xffffffff in every sample.
	if (s0 != 0xffffffff || s1 != 0xffffffff || s2 != 0xffffffff)
		return 0;
	if (file_size && table_offset > file_size)
		return 0;

	return 1;
}

enumError DecodeRedSteel2Abe_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data)
		return EINVAL;
	if (!IsRedSteel2Abe (data, size, file_size))
		return EINVAL;

	u32 version      = rd_le32 (data + 4);
	u32 entry_count  = rd_le32 (data + 8);
	u32 field_c      = rd_le32 (data + 12);
	u32 field_d      = rd_le32 (data + 16);
	u32 zero_a       = rd_le32 (data + 20);
	u32 field_e      = rd_le32 (data + 24);
	u32 zero_b       = rd_le32 (data + 28);
	u32 table_offset = rd_le32 (data + 32);
	u32 field_g      = rd_le32 (data + 36);
	u32 field_h      = rd_le32 (data + 40);
	u32 field_i      = rd_le32 (data + 44);
	u32 field_j      = rd_le32 (data + 48);

	fprintf (f, "# Ubisoft \"ABE\" bigfile (Red Steel 2) -- header only\n");
	fprintf (f, "# The directory-tree table at 'table_offset' is NOT decoded;\n");
	fprintf (f, "# see lib-redsteel2.h for what was and was not reverse-engineered.\n");
	fprintf (f, "\n");
	fprintf (f, "version      = %u\n", version);
	fprintf (f, "entry_count  = %u\n", entry_count);
	fprintf (f, "field_c      = %u  # 0x%x (unexplained; 1 or 2 observed)\n", field_c, field_c);
	fprintf (f, "field_d      = %u  # 0x%x (unexplained)\n", field_d, field_d);
	fprintf (f, "zero_a       = %u  # 0x%x (always 0 observed)\n", zero_a, zero_a);
	fprintf (f, "field_e      = %u  # 0x%x (constant 5336 in every sample seen)\n", field_e, field_e);
	fprintf (f, "zero_b       = %u  # 0x%x (always 0 observed)\n", zero_b, zero_b);
	fprintf (f, "table_offset = 0x%x\n", table_offset);
	fprintf (f, "field_g      = %u  # 0x%x (unexplained)\n", field_g, field_g);
	fprintf (f, "field_h      = %u  # 0x%x (unexplained)\n", field_h, field_h);
	fprintf (f, "field_i      = %u  # 0x%x (unexplained)\n", field_i, field_i);
	fprintf (f, "field_j      = %u  # 0x%x (unexplained)\n", field_j, field_j);

	if (table_offset && (u64)table_offset + 32 <= size)
	{
		fprintf (f, "\n# first 32 bytes at table_offset (directory table, structure not decoded):\n# ");
		for (uint i = 0; i < 32; i++)
			fprintf (f, "%02x ", data[table_offset + i]);
		fprintf (f, "\n");
	}

	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (2) ".rel" Nintendo/CodeWarrior REL relocatable module (big endian)

#define REL_HEADER_MIN_SIZE  0x40 // through unresolved_offset; align/bss_align/fix_size are v2/v3 extras
#define REL_MAX_SECTIONS     4096

int IsRedSteel2Rel (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < REL_HEADER_MIN_SIZE)
		return 0;

	u32 next            = rd_be32 (data + 4);
	u32 prev             = rd_be32 (data + 8);
	u32 num_sections     = rd_be32 (data + 12);
	u32 section_info_off = rd_be32 (data + 16);
	u32 version          = rd_be32 (data + 28);

	// Unloaded on-disc modules always have these zeroed (runtime linked
	// list pointers); confirmed in every sample.
	if (next != 0 || prev != 0)
		return 0;
	if (!num_sections || num_sections > REL_MAX_SECTIONS)
		return 0;
	// Only version 3 observed on this disc, but 1/2 are documented
	// upstream variants of the same public format, so accept 1..3.
	if (!version || version > 3)
		return 0;
	if (section_info_off < REL_HEADER_MIN_SIZE)
		return 0;
	if (file_size)
	{
		u64 table_end = (u64)section_info_off + (u64)num_sections * 8;
		if (table_end > file_size)
			return 0;
	}

	return 1;
}

enumError DecodeRedSteel2Rel_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data)
		return EINVAL;
	if (!IsRedSteel2Rel (data, size, file_size))
		return EINVAL;

	u32 id                = rd_be32 (data + 0);
	u32 num_sections      = rd_be32 (data + 12);
	u32 section_info_off  = rd_be32 (data + 16);
	u32 name_offset       = rd_be32 (data + 20);
	u32 name_size         = rd_be32 (data + 24);
	u32 version           = rd_be32 (data + 28);
	(void) name_offset;
	(void) name_size;
	u32 bss_size          = rd_be32 (data + 32);
	u32 rel_offset        = rd_be32 (data + 36);
	u32 imp_offset        = rd_be32 (data + 40);
	u32 imp_size          = rd_be32 (data + 44);
	u8  prolog_section     = data[48];
	u8  epilog_section     = data[49];
	u8  unresolved_section = data[50];
	u8  bss_section        = data[51];
	u32 prolog_offset     = rd_be32 (data + 52);
	u32 epilog_offset     = rd_be32 (data + 56);
	u32 unresolved_offset = rd_be32 (data + 60);

	fprintf (f, "# Nintendo/CodeWarrior REL relocatable module\n");
	fprintf (f, "\n");
	fprintf (f, "id                = %u\n", id);
	fprintf (f, "version           = %u\n", version);
	fprintf (f, "num_sections      = %u\n", num_sections);
	fprintf (f, "section_info_off  = 0x%x\n", section_info_off);
	// name_offset/name_size are part of the public REL header spec, but on
	// every sample seen on this disc they point at binary section content
	// (PowerPC code bytes), not a printable name string -- these modules
	// evidently ship with no module name set. Reported raw, not decoded as
	// text, to avoid asserting an interpretation that isn't confirmed here.
	fprintf (f, "name_offset       = 0x%x  (raw; not confirmed to be a string on this title)\n", name_offset);
	fprintf (f, "name_size         = %u\n", name_size);
	fprintf (f, "bss_size          = %u\n", bss_size);
	fprintf (f, "rel_offset        = 0x%x\n", rel_offset);
	fprintf (f, "imp_offset        = 0x%x\n", imp_offset);
	fprintf (f, "imp_size          = %u\n", imp_size);
	fprintf (f, "prolog_section    = %u  prolog_offset    = 0x%x\n", prolog_section, prolog_offset);
	fprintf (f, "epilog_section    = %u  epilog_offset    = 0x%x\n", epilog_section, epilog_offset);
	fprintf (f, "unresolved_section= %u  unresolved_offset= 0x%x\n", unresolved_section, unresolved_offset);
	fprintf (f, "bss_section       = %u\n", bss_section);

	if (version >= 2 && size >= 0x48)
	{
		u32 align     = rd_be32 (data + 64);
		u32 bss_align = rd_be32 (data + 68);
		fprintf (f, "align             = %u\n", align);
		fprintf (f, "bss_align         = %u\n", bss_align);
	}
	if (version >= 3 && size >= 0x4c)
	{
		u32 fix_size = rd_be32 (data + 72);
		fprintf (f, "fix_size          = 0x%x\n", fix_size);
	}

	fprintf (f, "\n# %u section(s), offset field's low bit is the \"executable\" flag\n", num_sections);
	fprintf (f, "# idx  offset      exec  length\n");
	for (u32 i = 0; i < num_sections; i++)
	{
		u64 rec_off = (u64)section_info_off + (u64)i * 8;
		if (rec_off + 8 > size)
			break;
		u32 raw_off = rd_be32 (data + rec_off);
		u32 length  = rd_be32 (data + rec_off + 4);
		fprintf (f, "%4u  0x%08x  %u     %u\n",
			i, raw_off & ~1u, raw_off & 1u, length);
	}

	fprintf (f, "\n# relocation table (rel_offset) and import table (imp_offset/imp_size)\n");
	fprintf (f, "# fixup opcodes are not decoded by this module\n");

	return ERR_OK;
}
