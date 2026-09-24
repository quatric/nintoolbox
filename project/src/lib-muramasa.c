// SPDX-License-Identifier: GPL-2.0+
// "Muramasa - The Demon Blade" (Wii) formats -- see lib-muramasa.h for
// exactly what is and is not understood about each of them.

#include "lib-muramasa.h"
#include "lib-nintendo.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

//-----------------------------------------------------------------------------
// (1) "FCMP" compressed container

#define FCMP_HEADER_SIZE  13
#define FCMP_RESERVED     0x12340000u

typedef struct fcmp_inner_map_t
{
	const char *inner_tag; // 4-byte inner sub-blob magic (not NUL terminated)
	const char *ext;       // on-disc extension it corresponds to
}
fcmp_inner_map_t;

// Confirmed 1:1 mapping between the inner sub-blob tag and the on-disc
// extension, checked against 307 real samples with zero exceptions -- see
// lib-muramasa.h.
static const fcmp_inner_map_t fcmp_inner_map[] =
{
	{ "FMBS", ".mbs" },
	{ "FTEX", ".ftx" },
	{ "EMBP", ".esb" },
	{ "NSBD", ".nsb" },
	{ "MLIB", ".abf" },
	{ "NMSB", ".nms" },
	{ 0, 0 }
};

static const fcmp_inner_map_t *fcmp_find_inner (const u8 *tag)
{
	for (const fcmp_inner_map_t *m = fcmp_inner_map; m->inner_tag; m++)
		if (!memcmp (tag, m->inner_tag, 4))
			return m;
	return 0;
}

int IsMuramasaFcmp (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < FCMP_HEADER_SIZE || memcmp (data, "FCMP", 4))
		return 0;

	u32 decomp_size = rd_le32 (data + 4);
	u32 reserved = rd_le32 (data + 8);
	if (reserved != FCMP_RESERVED)
		return 0;

	// Lower nibble of the flag byte is confirmed constant (0xf) across
	// every sample; the upper nibble varies (0xf/0xe/0x5 observed).
	if ((data[12] & 0xf) != 0xf)
		return 0;

	// Decompressed size must be at least as large as the compressed
	// payload that remains in the file -- confirmed true for every real
	// sample checked (never smaller).
	if (file_size >= FCMP_HEADER_SIZE && decomp_size < file_size - FCMP_HEADER_SIZE)
		return 0;

	// The inner sub-blob tag, if visible, must be one of the confirmed
	// six -- but don't require it to be visible when only a short probe
	// buffer is available.
	if (size >= FCMP_HEADER_SIZE + 4 && !fcmp_find_inner (data + FCMP_HEADER_SIZE))
		return 0;

	return 1;
}

enumError DecodeMuramasaFcmp_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsMuramasaFcmp (data, size, file_size))
		return EINVAL;

	u32 decomp_size = rd_le32 (data + 4);
	u8 flag = data[12];

	fprintf (f, "# Muramasa - The Demon Blade FCMP compressed container\n");
	fprintf (f, "decompressed_size = %u (0x%x)\n", decomp_size, decomp_size);
	fprintf (f, "reserved = 0x%08x  # constant in every sample seen\n", rd_le32 (data + 8));
	fprintf (f, "flag = 0x%02x\n", flag);

	if (size >= FCMP_HEADER_SIZE + 4)
	{
		const fcmp_inner_map_t *m = fcmp_find_inner (data + FCMP_HEADER_SIZE);
		if (m)
			fprintf (f, "inner_tag = \"%.4s\"  # matches on-disc extension %s\n",
				data + FCMP_HEADER_SIZE, m->ext);
		else
			fprintf (f, "inner_tag = \"%.4s\"  # NOT one of the confirmed tags\n",
				data + FCMP_HEADER_SIZE);
	}

	fprintf (f, "# compressed payload NOT decoded -- Yaz0/Yaz1 (this project's existing\n");
	fprintf (f, "# LZ77 decompressor) was ruled out (different header layout/magic), and a\n");
	fprintf (f, "# from-scratch standalone Yaz0-bitstream re-implementation correctly\n");
	fprintf (f, "# decoded the initial literal run (matching the inner tag above) but\n");
	fprintf (f, "# produced out-of-range back-reference distances on the first match\n");
	fprintf (f, "# token, ruling out standard Yaz0/Yaz1 token encoding specifically. The\n");
	fprintf (f, "# bespoke token/distance encoding was not reverse-engineered further --\n");
	fprintf (f, "# see lib-muramasa.h for the full account of what was tried.\n");

	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (2) ".otb" "OTB " table (header only)

int IsMuramasaOtb (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < 16 || memcmp (data, "OTB ", 4))
		return 0;

	u32 body_size = rd_le32 (data + 4);
	u32 header_size = rd_le32 (data + 8);
	if ((u64) body_size + header_size != file_size)
		return 0;

	u32 count = rd_le32 (data + 12);
	if (!count || count > 1000000)
		return 0;

	return 1;
}

enumError DecodeMuramasaOtb_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsMuramasaOtb (data, size, file_size))
		return EINVAL;

	fprintf (f, "# Muramasa - The Demon Blade .otb table\n");
	fprintf (f, "body_size = %u (0x%x)\n", rd_le32 (data + 4), rd_le32 (data + 4));
	fprintf (f, "header_size = %u (0x%x)  # body_size + header_size == file_size\n",
		rd_le32 (data + 8), rd_le32 (data + 8));
	fprintf (f, "count = %u (0x%x)\n", rd_le32 (data + 12), rd_le32 (data + 12));
	fprintf (f, "# entry table body not reverse-engineered (too few distinct samples on\n");
	fprintf (f, "# this disc -- see lib-muramasa.h) -- only the header is decoded\n");
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (3) ".nsi" "NSI " sound info table (header only)

int IsMuramasaNsi (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < 12 || memcmp (data, "NSI ", 4))
		return 0;

	u32 body_size = rd_le32 (data + 4);
	u32 tail_size = rd_le32 (data + 8);
	if ((u64) body_size + tail_size != file_size)
		return 0;

	return 1;
}

enumError DecodeMuramasaNsi_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsMuramasaNsi (data, size, file_size))
		return EINVAL;

	fprintf (f, "# Muramasa - The Demon Blade .nsi sound info table\n");
	fprintf (f, "body_size = %u (0x%x)\n", rd_le32 (data + 4), rd_le32 (data + 4));
	fprintf (f, "tail_size = %u (0x%x)  # body_size + tail_size == file_size\n",
		rd_le32 (data + 8), rd_le32 (data + 8));
	fprintf (f, "# entry table body not reverse-engineered (only one real sample on this\n");
	fprintf (f, "# disc -- see lib-muramasa.h) -- only the header is decoded\n");
	return ERR_OK;
}
