// SPDX-License-Identifier: GPL-2.0+
// "Aqua Panic!" (Wii) formats -- see lib-aquapanic.h for exactly what is
// and is not understood about each of them.

#include "lib-aquapanic.h"
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

static float rd_le_float (const u8 *p)
{
	u32 bits = rd_le32 (p);
	float val;
	memcpy (&val, &bits, sizeof (val));
	return val;
}

//-----------------------------------------------------------------------------
// (1) ".rck" / ".spa" "RKET" resource container (outer header only)

int IsAquaPanicRket (const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!data || size < 24)
		return 0;
	return !memcmp (data, "RKET", 4) && rd_le32 (data + 4) == 0;
}

enumError DecodeAquaPanicRket_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!f || !data || size < 24)
		return EINVAL;

	fprintf (f, "# Aqua Panic! RKET resource container (.rck / .spa)\n");
	fprintf (f, "# Shared outer header only -- resource-graph/scene-node structure\n");
	fprintf (f, "# after the header not reverse-engineered; see lib-aquapanic.h note (1).\n");
	fprintf (f, "hash = 0x%x\n", rd_le32 (data + 8));
	fprintf (f, "version = %u\n", rd_le16 (data + 12));
	fprintf (f, "flags = 0x%x\n", rd_le16 (data + 14));
	fprintf (f, "size_field = %u  # does not equal file_size, see note (1)\n", rd_le32 (data + 20));
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (2) ".mat" "MATF" material chunk table

int IsAquaPanicMat (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < 24 || file_size < 24)
		return 0;
	if (memcmp (data, "MATF", 4) || rd_le32 (data + 4) != 8)
		return 0;

	// Require the first chunk tag to look structurally valid and to fit
	// within the file -- this is what distinguishes a real MATF file
	// from an unrelated file that happens to start with the same 16-byte
	// prefix (same approach as IsSpookyAst()'s first-chunk check).
	for (int i = 0; i < 4; i++)
	{
		u8 c = data[16 + i];
		if (c != ' ' && !isalnum (c))
			return 0;
	}
	u32 payload = rd_le32 (data + 20);
	return (u64)24 + payload <= file_size;
}

enumError DecodeAquaPanicMat_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!f || !data || size < 16)
		return EINVAL;

	fprintf (f, "# Aqua Panic! MATF material chunk table (.mat)\n");
	fprintf (f, "count_a = %u\n", rd_le32 (data + 8));
	fprintf (f, "count_b = %u\n", rd_le32 (data + 12));
	fprintf (f, "# Chunk table (tag, payload_size); payload contents not\n");
	fprintf (f, "# reverse-engineered, see lib-aquapanic.h note (2).\n");

	size_t off = 16;
	while (off + 8 <= size)
	{
		char tag[5];
		memcpy (tag, data + off, 4);
		tag[4] = 0;
		u32 payload = rd_le32 (data + off + 4);
		fprintf (f, "chunk \"%s\" payload_size=%u offset=0x%zx\n", tag, payload, off);
		off += 8 + payload;
		if (off > size)
			break;
	}
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (3) ".mb2" "BNAM" name-string table

int IsAquaPanicMb2 (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < 17 || file_size < 17)
		return 0;
	if (memcmp (data, "BNAM", 4))
		return 0;
	if (rd_le32 (data + 4) != file_size - 8)
		return 0;
	if (rd_le32 (data + 12) != 1)
		return 0;
	if (data[16] != 0)
		return 0;
	if (size < 21)
		return 1; // header confirmed; not enough buffered to check the first entry

	// Require the first name entry to look structurally valid and to fit
	// within the file.
	u32 len = rd_le32 (data + 17);
	return len > 0 && (u64)21 + len <= file_size;
}

enumError DecodeAquaPanicMb2_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!f || !data || size < 17)
		return EINVAL;

	fprintf (f, "# Aqua Panic! BNAM name-string table (.mb2)\n");
	fprintf (f, "count = %u\n", rd_le32 (data + 8));

	size_t off = 17;
	int idx = 0;
	while (off + 4 <= size)
	{
		u32 len = rd_le32 (data + off);
		if (!len || off + 4 + len > size)
			break;
		fprintf (f, "name[%d] = \"", idx++);
		print_escaped (f, data + off + 4, len);
		fprintf (f, "\"\n");
		off += 4 + len;
	}
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (4) ".vis" fixed visibility/flag record

int IsAquaPanicVis (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < 24 || file_size != 24)
		return 0;
	return rd_le32 (data) == 1 && rd_le32 (data + 4) == 1 && rd_le32 (data + 8) == 0
		&& rd_le32 (data + 12) == 1 && rd_le32 (data + 16) == 0 && rd_le32 (data + 20) == 0;
}

enumError DecodeAquaPanicVis_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	(void)data;
	(void)file_size;
	if (!f || size != 24)
		return EINVAL;

	fprintf (f, "# Aqua Panic! visibility/flag record (.vis)\n");
	fprintf (f, "# Fixed record, identical across every one of 100 real samples.\n");
	fprintf (f, "field_0 = 1\n");
	fprintf (f, "field_1 = 1\n");
	fprintf (f, "field_2 = 0\n");
	fprintf (f, "field_3 = 1\n");
	fprintf (f, "field_4 = 0\n");
	fprintf (f, "field_5 = 0\n");
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (5) ".lit" single-light record

int IsAquaPanicLit (const u8 *data, size_t size, size_t file_size)
{
	if (!data || (file_size != 12 && file_size != 48) || size < 8)
		return 0;
	if (rd_le32 (data) != 0x20031126)
		return 0;
	u32 count = rd_le32 (data + 4);
	if (file_size == 12)
		return count == 0;
	return count == 1;
}

enumError DecodeAquaPanicLit_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!f || !data)
		return EINVAL;

	fprintf (f, "# Aqua Panic! single-light record (.lit)\n");
	fprintf (f, "date_tag = 0x%x\n", rd_le32 (data));
	u32 count = size >= 8 ? rd_le32 (data + 4) : 0;
	fprintf (f, "count = %u\n", count);
	if (size == 12 || !count)
	{
		fprintf (f, "# empty short-form record -- no light data present\n");
		return ERR_OK;
	}
	if (size < 48)
		return ERR_OK;

	fprintf (f, "color_r = %f\n", rd_le_float (data + 16));
	fprintf (f, "color_g = %f\n", rd_le_float (data + 20));
	fprintf (f, "color_b = %f\n", rd_le_float (data + 24));
	fprintf (f, "pos_x = %f\n", rd_le_float (data + 28));
	fprintf (f, "pos_y = %f\n", rd_le_float (data + 32));
	fprintf (f, "pos_z = %f\n", rd_le_float (data + 36));
	return ERR_OK;
}
