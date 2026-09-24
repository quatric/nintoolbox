// SPDX-License-Identifier: GPL-2.0+
// "Bermuda Triangle - Saving the Coral" (Wii) formats -- see
// lib-bermudatriangle.h for exactly what is and is not understood about
// each of them.

#include "lib-bermudatriangle.h"
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
		if (c == '\\')
			fputs ("\\\\", f);
		else if (c >= 0x20 && c < 0x7f)
			fputc (c, f);
		else
			fprintf (f, "\\x%02x", c);
	}
}

static int is_ascii_run (const u8 *data, size_t len)
{
	if (!len)
		return 0;
	for (size_t i = 0; i < len; i++)
		if (!isprint (data[i]) && data[i] != '\t')
			return 0;
	return 1;
}

//-----------------------------------------------------------------------------
// (0) ".MWT" "GDATAVERSION" outer resource envelope

enum { BT_MWT_MAGIC_LEN = 12, BT_MWT_HEADER_SIZE = 0x40 };
#define BT_MWT_MAGIC "GDATAVERSION"
#define BT_CAMELOT_BANK_MAGIC_LE 0x0020af30u // big-endian on disk: 00 20 af 30

int IsBermudaMwt (const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!data || size < BT_MWT_HEADER_SIZE)
		return 0;
	if (memcmp (data, BT_MWT_MAGIC, BT_MWT_MAGIC_LEN))
		return 0;
	return rd_le32 (data + BT_MWT_MAGIC_LEN) == 2;
}

enumError DecodeBermudaMwt_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsBermudaMwt (data, size, file_size))
		return EINVAL;

	u32 payload_size = rd_le32 (data + 0x10);
	u32 width        = rd_le32 (data + 0x14);
	u32 height       = rd_le32 (data + 0x18);
	u32 flags        = rd_le32 (data + 0x1c);

	fprintf (f, "# Bermuda Triangle GDATAVERSION resource envelope (.MWT)\n");
	fprintf (f, "version = 2\n");
	fprintf (f, "payload_size = %u  # bytes following this 0x%x-byte header\n",
		payload_size, BT_MWT_HEADER_SIZE);
	fprintf (f, "width = %u\n", width);
	fprintf (f, "height = %u\n", height);
	fprintf (f, "flags = %u  # meaning not confirmed\n", flags);

	if ((u64) BT_MWT_HEADER_SIZE + payload_size > size)
	{
		fprintf (f, "# payload truncated/out-of-bounds -- not inspected further\n");
		return ERR_OK;
	}

	const u8 *payload = data + BT_MWT_HEADER_SIZE;
	if (payload_size >= 4 && rd_be32 (payload) == BT_CAMELOT_BANK_MAGIC_LE)
		fprintf (f,
			"payload = Camelot-style GX texture bank (magic 0x0020af30, "
			"already supported -- see lib-camtexbank.c)\n");
	else
		fprintf (f, "payload = unrecognized (%u bytes)\n", payload_size);

	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (1) ".MWG" / ".MSP" "PLANETG" tagged-object serialization

int IsBermudaPlanetG (const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!data || size < 8)
		return 0;
	u32 len = rd_le32 (data);
	if (!len || len > 64 || 4 + (u64) len > size)
		return 0;
	if (!is_ascii_run (data + 4, len))
		return 0;
	return !memcmp (data + 4, "PLANETG_", 8 <= len ? 8 : len);
}

// Heuristic walk: repeatedly read a u32; if it is a plausible length for an
// immediately-following printable ASCII run, print that run as a tag/string
// node, otherwise print the u32 itself as a raw numeric field. This finds
// every real tag name byte-for-byte (confirmed against GameSytem.MWG,
// Continue.MWG, Title.MWG and LOAD.MSP) but does not claim to recover the
// exact parent/child tree shape or the meaning of the numeric fields -- see
// note (1) in the header.
enumError DecodeBermudaPlanetG_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsBermudaPlanetG (data, size, file_size))
		return EINVAL;

	fprintf (f, "# Bermuda Triangle PLANETG tagged-object resource\n"
		"# heuristic walk: length-prefixed ASCII runs are tags/strings,\n"
		"# everything else is printed as a raw u32 field (see lib-bermudatriangle.h)\n");

	size_t pos = 0;
	while (pos + 4 <= size)
	{
		u32 val = rd_le32 (data + pos);
		if (val && val <= 256 && pos + 4 + (u64) val <= size
			&& is_ascii_run (data + pos + 4, val))
		{
			fprintf (f, "tag[0x%zx] len=%u \"", pos, val);
			print_escaped (f, data + pos + 4, val);
			fputs ("\"\n", f);
			pos += 4 + val;
		}
		else
		{
			fprintf (f, "field[0x%zx] = %u (0x%x)\n", pos, val, val);
			pos += 4;
		}
	}

	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (2) ".PKI" "IMAGE_WII_COMPACT_FILE_VERSION_1" texture-pack container

enum { BT_PKI_MAGIC_LEN = 32, BT_PKI_HEADER_SIZE = 36 };
#define BT_PKI_MAGIC "IMAGE_WII_COMPACT_FILE_VERSION_1"

int IsBermudaPki (const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!data || size < BT_PKI_HEADER_SIZE)
		return 0;
	return !memcmp (data, BT_PKI_MAGIC, BT_PKI_MAGIC_LEN);
}

enumError DecodeBermudaPki_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsBermudaPki (data, size, file_size))
		return EINVAL;

	u32 entry_count = rd_le32 (data + BT_PKI_MAGIC_LEN);

	fprintf (f, "# Bermuda Triangle IMAGE_WII_COMPACT texture-pack container (.PKI)\n");
	fprintf (f, "entry_count = %u\n\n", entry_count);

	size_t pos = BT_PKI_HEADER_SIZE;
	for (u32 i = 0; i < entry_count; i++)
	{
		if (pos + 4 > size)
		{
			fprintf (f, "# entry table truncated at entry %u -- stopping\n", i);
			break;
		}
		u32 name_len = rd_le32 (data + pos);
		pos += 4;
		if ((u64) pos + name_len + 8 > size)
		{
			fprintf (f, "# entry %u name/trailer out-of-bounds -- stopping\n", i);
			break;
		}
		const u8 *name = data + pos;
		pos += name_len;
		u32 data_size   = rd_le32 (data + pos); pos += 4;
		u32 data_offset = rd_le32 (data + pos); pos += 4;

		fprintf (f, "entry[%u] name=\"", i);
		print_escaped (f, name, name_len);
		fprintf (f, "\" size=%u offset=0x%x\n", data_size, data_offset);
	}

	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (3) ".pgf" font resource

enum { BT_PGF_MIN_HEADER = 8 };

int IsBermudaPgf (const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!data || size < BT_PGF_MIN_HEADER)
		return 0;
	u32 name_len = rd_le32 (data);
	if (!name_len || name_len > 64 || 4 + (u64) name_len + 4 > size)
		return 0;
	if (!is_ascii_run (data + 4, name_len))
		return 0;
	u32 point_size = rd_le32 (data + 4 + name_len);
	// Every real sample has a plausible point size (8..96); reject wild
	// values to avoid false-positiving on other length-prefixed-string
	// formats such as the PLANETG tagged objects above.
	return point_size >= 4 && point_size <= 256;
}

enumError DecodeBermudaPgf_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsBermudaPgf (data, size, file_size))
		return EINVAL;

	u32 name_len = rd_le32 (data);
	const u8 *name = data + 4;
	u32 point_size = rd_le32 (data + 4 + name_len);

	fprintf (f, "# Bermuda Triangle font resource (.pgf)\n");
	fprintf (f, "family = \"");
	print_escaped (f, name, name_len);
	fprintf (f, "\"\n");
	fprintf (f, "point_size = %u\n", point_size);
	fprintf (f, "# per-glyph offset table follows; not decoded, see lib-bermudatriangle.h\n");

	(void) size;
	(void) file_size;
	return ERR_OK;
}
