// SPDX-License-Identifier: GPL-2.0+
// Casper's Scare School: Spooky Sports Day (Wii, A2M) .tex texture format.
// See lib-casper.h for exactly what is and is not understood.

#include "lib-casper.h"
#include "lib-nintendo.h"
#include <stdio.h>
#include <string.h>

//-----------------------------------------------------------------------------
///////////////////////////////   .tex header   ///////////////////////////////
//-----------------------------------------------------------------------------

int IsCasperTEX (const u8 *data, size_t size, size_t file_size)
{
	if (size < CASPER_TEX_HEADER_SIZE || file_size < CASPER_TEX_HEADER_SIZE)
		return 0;

	if (data[0] != CASPER_TEX_MAGIC0 || data[1] != CASPER_TEX_MAGIC1)
		return 0;

	// bytes 2..3: always zero in every sample seen
	if (data[2] || data[3])
		return 0;

	const u32 pixel_format = rd_be32 (data + 4);
	const u32 payload_size = rd_be32 (data + 8);
	const u32 width = rd_be32 (data + 12);
	const u32 height = rd_be32 (data + 16);
	const u32 flag1 = rd_be32 (data + 20);

	// Every sample seen used format codes 9..11; allow a little headroom
	// for formats simply not present in the sample set, but reject clearly
	// implausible values (this field also doubles as a filter against
	// random data matching the 04 02 00 00 prefix by chance).
	if (!pixel_format || pixel_format > 32)
		return 0;

	if (!width || !height || width > 0x2000 || height > 0x2000)
		return 0;

	if (flag1 != 1)
		return 0; // constant in every sample seen; reject if it ever differs

	if (!payload_size || payload_size > 0x10000000)
		return 0;

	// The full payload must fit in the real file, even if we were only
	// handed a short probe prefix.
	if ((u64)CASPER_TEX_HEADER_SIZE + payload_size > file_size)
		return 0;

	return 1;
}

//-----------------------------------------------------------------------------

enumError DecodeCasperTEX_Text (FILE *f, const u8 *data, size_t size)
{
	if (size < CASPER_TEX_HEADER_SIZE)
		return ERROR0 (ERR_INVALID_DATA, "Casper .tex: file too short for header\n");

	const u32 pixel_format = rd_be32 (data + 4);
	const u32 payload_size = rd_be32 (data + 8);
	const u32 width = rd_be32 (data + 12);
	const u32 height = rd_be32 (data + 16);
	const u32 flag1 = rd_be32 (data + 20);

	fprintf (f, "#\n# Casper's Scare School .tex texture (reverse-engineered)\n#\n\n");
	fprintf (f, "pixel-format   = %u\n", pixel_format);
	fprintf (f, "width          = %u\n", width);
	fprintf (f, "height         = %u\n", height);
	fprintf (f, "flag1          = %u\n", flag1);
	fprintf (f, "payload-offset = 0x%x\n", CASPER_TEX_HEADER_SIZE);
	fprintf (f, "payload-size   = %u\n", payload_size);
	fprintf (f,
		"\n# pixel payload is NOT decoded: the exact GX-style pixel\n"
		"# format numbering used by this title was not identified.\n");

	return ERR_OK;
}
