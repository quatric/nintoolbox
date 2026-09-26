// SPDX-License-Identifier: GPL-2.0+
// "Mercury Meltdown Revolution" (Wii) formats -- see lib-mercurymeltdown.h
// for exactly what is and is not understood about each of them.

#include "lib-mercurymeltdown.h"
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
// (1) ".zen" scene file

int IsMercuryZen (const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!data || size < 16)
		return 0;
	return !memcmp (data, "DAED", 4);
}

enumError DecodeMercuryZen_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsMercuryZen (data, size, file_size))
		return EINVAL;

	fprintf (f, "# Mercury Meltdown Revolution scene file (.zen)\n");
	fprintf (f, "field_a = 0x%x  # little-endian\n", rd_le32 (data + 4));
	fprintf (
		f, "zero = 0x%x  # little-endian, always 0 in every sample seen\n", rd_le32 (data + 8));
	fprintf (f, "count_a = %u  # little-endian u16\n", rd_le16 (data + 12));
	fprintf (f, "count_b = %u  # little-endian u16\n", rd_le16 (data + 14));

	if (size > 16)
	{
		size_t max_len = size - 16;
		if (max_len > 512)
			max_len = 512;
		fprintf (f, "source_path = \"");
		print_escaped (f, data + 16, max_len);
		fprintf (f, "\"\n");
	}
	fprintf (
		f, "# fields after the source path not reverse-engineered -- see lib-mercurymeltdown.h\n");
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (2) ".col" / ".cam" "COL0" collision/camera table

int IsMercuryCol (const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!data || size < 8)
		return 0;
	return !memcmp (data, "COL0", 4);
}

enumError DecodeMercuryCol_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsMercuryCol (data, size, file_size))
		return EINVAL;

	u32 count = rd_le32 (data + 4);
	fprintf (f, "# Mercury Meltdown Revolution COL0 collision/camera table (.col/.cam)\n");
	fprintf (f, "count = %u  # little-endian\n", count);

	if (size == 12 && count == 0)
	{
		fprintf (f, "# empty instance -- no collision/camera data in this file\n");
		return ERR_OK;
	}

	if (size > 8)
	{
		// Leading NUL-terminated name of the first record, when present.
		// Per-record tagged property data after the name is NOT
		// reverse-engineered -- see lib-mercurymeltdown.h note (2).
		size_t i = 8, start = 8;
		while (i < size && data[i])
			i++;
		if (i > start && i < size)
		{
			fprintf (f, "first_record_name = \"");
			print_escaped (f, data + start, i - start);
			fprintf (f, "\"\n");
		}
	}
	fprintf (f, "# record property data not reverse-engineered -- see lib-mercurymeltdown.h\n");
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (3) ".pst" "TSPA" paletted texture

int IsMercuryPst (const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!data || size < 0x1c)
		return 0;
	if (memcmp (data, "TSPA", 4))
		return 0;
	return !memcmp (data + 9, "CGN", 3);
}

enumError DecodeMercuryPst_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsMercuryPst (data, size, file_size))
		return EINVAL;

	fprintf (f, "# Mercury Meltdown Revolution TSPA paletted texture (.pst)\n");
	fprintf (f, "field_a = 0x%x  # big-endian\n", rd_be32 (data + 4));
	fprintf (f, "tag = \" CGN\"\n");
	fprintf (f, "field_b = 0x%x  # big-endian\n", rd_be32 (data + 0x0c));
	fprintf (f, "width = %u  # big-endian\n", rd_be32 (data + 0x10));
	fprintf (f, "height = %u  # big-endian\n", rd_be32 (data + 0x14));
	fprintf (f, "# pixel data layout not reverse-engineered -- see lib-mercurymeltdown.h\n");
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (4) ".mat" material float-record table (extension-only, unconfirmed)

int IsMercuryMat (const u8 *data, size_t size, size_t file_size)
{
	// No fixed magic or confirmed record layout was found in any real
	// sample pulled from this disc -- see lib-mercurymeltdown.h note (4).
	// This probe is intentionally NOT wired into the magic-based
	// auto-detector in lib-file.c (it would false-positive on arbitrary
	// binary/float data); it exists only so the decoder can be reached
	// via explicit extension match, same as The Dog Island's .sci/.qci
	// and Zack & Wiki's .ssd.
	(void)data;
	return size > 0 && size == file_size;
}

enumError DecodeMercuryMat_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!f || !data || !size)
		return EINVAL;

	fprintf (f, "# Mercury Meltdown Revolution material table (.mat)\n");
	fprintf (f, "# NOT reverse-engineered -- no confirmed magic or record layout;\n");
	fprintf (f, "# see lib-mercurymeltdown.h note (4). Reporting leading fields raw.\n");
	if (size >= 4)
		fprintf (f, "field_0 = 0x%x\n", rd_be32 (data));
	if (size >= 16)
		fprintf (f, "field_hash = 0x%x\n", rd_be32 (data + 12));
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (5) ".nav" navigation-mesh table (extension-only, unconfirmed)

int IsMercuryNav (const u8 *data, size_t size, size_t file_size)
{
	// Same reasoning as IsMercuryMat() above -- magic-less, no confirmed
	// header shape (and a meaningful fraction of real samples are
	// zero-length), so this is extension-recognized only. A zero-length
	// file is accepted too, since it is a confirmed valid on-disc state
	// for this extension.
	(void)data;
	return file_size == size;
}

enumError DecodeMercuryNav_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	(void)data;
	(void)file_size;
	if (!f)
		return EINVAL;

	fprintf (f, "# Mercury Meltdown Revolution navigation-mesh table (.nav)\n");
	if (!size)
	{
		fprintf (f, "# empty file -- a confirmed valid state for this extension\n");
		return ERR_OK;
	}
	fprintf (f, "# NOT reverse-engineered -- no confirmed header shape;\n");
	fprintf (f, "# see lib-mercurymeltdown.h note (5).\n");
	return ERR_OK;
}
