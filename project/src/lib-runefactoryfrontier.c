// SPDX-License-Identifier: GPL-2.0+
// "Rune Factory: Frontier" (Wii) formats -- see lib-runefactoryfrontier.h
// for exactly what is and is not understood about each of them.

#include "lib-runefactoryfrontier.h"
#include "lib-nintendo.h"
#include <string.h>
#include <ctype.h>

enum { RFF_HX_HEADER_SIZE = 0x20 };

//-----------------------------------------------------------------------------
// shared helpers

static int is_hx_magic ( const u8 *data, size_t size, const char *tag4, u32 version )
{
	if ( !data || size < RFF_HX_HEADER_SIZE )
		return 0;
	if ( memcmp (data, tag4, 4) )
		return 0;
	// remaining 4 bytes are an ASCII decimal version, e.g. "0001"/"0002"
	char buf[5];
	memcpy (buf, data + 4, 4);
	buf[4] = 0;
	for ( int i = 0; i < 4; i++ )
		if ( !isdigit ((unsigned char)buf[i]) )
			return 0;
	return (u32) strtoul (buf, 0, 10) == version;
}

static void print_escaped ( FILE *f, const u8 *data, size_t len )
{
	for ( size_t i = 0; i < len; i++ )
	{
		u8 c = data[i];
		if ( !c )
			break;
		if ( c == '\\' )
			fputs ("\\\\", f);
		else if ( c >= 0x20 && c < 0x7f )
			fputc (c, f);
		else
			fprintf (f, "\\x%02x", c);
	}
}

// Scan for runs of >=4 printable ASCII characters, NUL or non-printable
// terminated -- used to pull entity/name strings out of the HX-family
// sub-formats whose post-header record layout is not reverse-engineered.
static void scan_name_strings ( FILE *f, const u8 *data, size_t size )
{
	size_t run_start = (size_t) -1;
	int printed = 0;
	for ( size_t i = RFF_HX_HEADER_SIZE; i <= size; i++ )
	{
		int printable = i < size && data[i] >= 0x20 && data[i] < 0x7f;
		if ( printable )
		{
			if ( run_start == (size_t) -1 )
				run_start = i;
		}
		else
		{
			if ( run_start != (size_t) -1 && i - run_start >= 4 )
			{
				if ( !printed )
				{
					fprintf (f, "\n# embedded printable-ASCII strings (heuristic scan, likely entity/bone names)\n");
					printed = 1;
				}
				fprintf (f, "string @0x%zx = \"", run_start);
				print_escaped (f, data + run_start, i - run_start);
				fprintf (f, "\"\n");
			}
			run_start = (size_t) -1;
		}
	}
}

//-----------------------------------------------------------------------------
// (1) HXTB0001 -- "HX Table" entry directory (.hvt / .Hvt)

enum { RFF_HXTB_ENTRY_SIZE = 0x20 };

int IsRFFHxtb ( const u8 *data, size_t size, size_t file_size )
{
	(void) file_size;
	return is_hx_magic (data, size, "HXTB", 1);
}

enumError DecodeRFFHxtb_Text ( FILE *f, const u8 *data, size_t size, size_t file_size )
{
	if ( !f || !data || !IsRFFHxtb (data, size, file_size) )
		return EINVAL;

	u32 entry_stride = rd_be32 (data + 0x08);
	u32 count        = rd_be32 (data + 0x0c);
	u32 table_size   = rd_be32 (data + 0x18);
	u32 total_size   = rd_be32 (data + 0x1c);

	fprintf (f, "# Rune Factory: Frontier \"HX Table\" entry directory (.hvt/.Hvt)\n");
	fprintf (f, "entry_stride = 0x%x  # big-endian, bytes per entry\n", entry_stride);
	fprintf (f, "entry_count = %u  # big-endian\n", count);
	fprintf (f, "table_region_size = 0x%x  # big-endian, == file_size - 0x20 in every sample seen\n", table_size);
	fprintf (f, "total_file_size = 0x%x  # big-endian, self-referential; actual file size = %zu\n", total_size, file_size);

	if ( entry_stride != RFF_HXTB_ENTRY_SIZE
		|| (u64) RFF_HX_HEADER_SIZE + (u64) count * entry_stride > size )
	{
		fprintf (f, "# entry table truncated/out-of-bounds/unexpected stride -- not decoded further\n");
		return ERR_OK;
	}

	fprintf (f, "\n# entry table (%u entries, 0x%x bytes each, starting at 0x%x)\n",
		count, entry_stride, RFF_HX_HEADER_SIZE);

	for ( u32 i = 0; i < count; i++ )
	{
		const u8 *e = data + RFF_HX_HEADER_SIZE + (size_t) i * entry_stride;
		size_t name_len = 0;
		while ( name_len < 0x10 && e[name_len] )
			name_len++;

		u32 hash      = rd_be32 (e + 0x10);
		u32 data_beg  = rd_be32 (e + 0x14);
		u32 data_end  = rd_be32 (e + 0x18);
		u32 reserved  = rd_be32 (e + 0x1c);

		fprintf (f, "entry[%u]: name = \"", i);
		print_escaped (f, e, name_len);
		fprintf (f, "\"  hash = 0x%08x  data = [0x%x,0x%x)  reserved = 0x%x\n",
			hash, data_beg, data_end, reserved);
	}

	fprintf (f, "\n# per-entity data-blob payload content not reverse-engineered\n");
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (2) Other confirmed "HX" family magics -- header-only decode

int IsRFFHxcb ( const u8 *data, size_t size, size_t file_size ) { (void) file_size; return is_hx_magic (data, size, "HXCB", 2); }
int IsRFFHxmb ( const u8 *data, size_t size, size_t file_size ) { (void) file_size; return is_hx_magic (data, size, "HXMB", 1); }
int IsRFFHxhb ( const u8 *data, size_t size, size_t file_size ) { (void) file_size; return is_hx_magic (data, size, "HXHB", 1); }
int IsRFFHxgb ( const u8 *data, size_t size, size_t file_size ) { (void) file_size; return is_hx_magic (data, size, "HXGB", 1); }
int IsRFFHxtp ( const u8 *data, size_t size, size_t file_size ) { (void) file_size; return is_hx_magic (data, size, "HXTP", 1); }

int IsRFFHxaa ( const u8 *data, size_t size, size_t file_size )
{
	(void) file_size;
	// .Hvb samples observed with either magic; treat both as the same
	// "HX Animation" sub-family.
	return is_hx_magic (data, size, "HXAA", 1) || is_hx_magic (data, size, "HXAB", 1);
}

enumError DecodeRFFHxGeneric_Text ( FILE *f, const u8 *data, size_t size, size_t file_size )
{
	if ( !f || !data || size < RFF_HX_HEADER_SIZE )
		return EINVAL;

	char tag[9];
	memcpy (tag, data, 8);
	tag[8] = 0;

	fprintf (f, "# Rune Factory: Frontier \"HX\" family resource (magic \"%s\")\n", tag);
	fprintf (f, "# generic HX-family header; per-type record layout beyond the header is not reverse-engineered\n");
	fprintf (f, "field_08 = 0x%x  # big-endian\n", rd_be32 (data + 0x08));
	fprintf (f, "field_0c = 0x%x  # big-endian\n", rd_be32 (data + 0x0c));
	fprintf (f, "field_10 = 0x%x  # big-endian\n", rd_be32 (data + 0x10));
	fprintf (f, "field_14 = 0x%x  # big-endian\n", rd_be32 (data + 0x14));
	fprintf (f, "field_18 = 0x%x  # big-endian\n", rd_be32 (data + 0x18));
	fprintf (f, "field_1c = 0x%x  # big-endian\n", rd_be32 (data + 0x1c));

	scan_name_strings (f, data, size);

	fprintf (f, "\n# body not reverse-engineered beyond the strings above -- see lib-runefactoryfrontier.h\n");
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (3) FBTI0001 model/motion section container (.Mod / .Mot)

enum { RFF_FBTI_HEADER_SIZE = 0x10, RFF_FBTI_ENTRY_SIZE = 0x08 };

int IsRFFFbti ( const u8 *data, size_t size, size_t file_size )
{
	(void) file_size;
	if ( !data || size < RFF_FBTI_HEADER_SIZE )
		return 0;
	if ( memcmp (data, "FBTI0001", 8) )
		return 0;
	u32 header_size = rd_be32 (data + 0x0c);
	return header_size == RFF_FBTI_HEADER_SIZE;
}

enumError DecodeRFFFbti_Text ( FILE *f, const u8 *data, size_t size, size_t file_size )
{
	if ( !f || !data || !IsRFFFbti (data, size, file_size) )
		return EINVAL;

	u32 count       = rd_be32 (data + 0x08);
	u32 header_size = rd_be32 (data + 0x0c);

	fprintf (f, "# Rune Factory: Frontier \"FBTI\" model/motion section container (.Mod/.Mot)\n");
	fprintf (f, "section_count = %u  # big-endian\n", count);
	fprintf (f, "header_size = 0x%x  # big-endian, always 0x10 in every sample seen\n", header_size);

	if ( (u64) header_size + (u64) count * RFF_FBTI_ENTRY_SIZE > size )
	{
		fprintf (f, "# section table truncated/out-of-bounds -- not decoded further\n");
		return ERR_OK;
	}

	fprintf (f, "\n# section table (%u entries, 8 bytes each, starting at 0x%x)\n", count, header_size);

	for ( u32 i = 0; i < count; i++ )
	{
		const u8 *e = data + header_size + (size_t) i * RFF_FBTI_ENTRY_SIZE;
		u32 ofs  = rd_be32 (e + 0x00);
		u32 dlen = rd_be32 (e + 0x04);

		fprintf (f, "section[%u]: offset = 0x%x  size = 0x%x  end = 0x%x", i, ofs, dlen, ofs + dlen);

		if ( (u64) ofs + 4 <= size && !memcmp (data + ofs, "HX", 2) )
		{
			char tag[9];
			memcpy (tag, data + ofs, 8 <= size - ofs ? 8 : size - ofs);
			tag[8] = 0;
			fprintf (f, "  # embedded HX-family resource, magic \"%s\"", tag);
		}
		fprintf (f, "\n");

		if ( (u64) ofs + dlen > size )
			fprintf (f, "  # WARNING: section out of bounds\n");
	}

	fprintf (f, "\n# per-section payload content (beyond an embedded HX header, when present) not reverse-engineered\n");
	return ERR_OK;
}
