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
	(void)file_size;
	if (!f || !data || !IsMercuryPst (data, size, file_size))
		return EINVAL;

	u32 version = rd_be32 (data + 4);
	u32 subtexture_count = rd_be32 (data + 0x0c);
	u32 entries_offset = rd_be32 (data + 0x10);
	u32 data_offset = rd_be32 (data + 0x14);

	fprintf (f, "# Mercury Meltdown Revolution TSPA paletted texture (.pst)\n");
	fprintf (f, "version = %u\n", version);
	fprintf (f, "platform = \" CGN\"\n");
	fprintf (f, "subtexture_count = %u\n", subtexture_count);
	fprintf (f, "entries_offset = 0x%x\n", entries_offset);
	fprintf (f, "data_offset = 0x%x\n", data_offset);

	if (entries_offset && entries_offset + (u64)subtexture_count * 16 <= size)
	{
		for (uint i = 0; i < subtexture_count; i++)
		{
			const u8 *entry = data + entries_offset + i * 16;
			u32 hash = rd_be32 (entry);
			u32 fmt_dims = rd_be32 (entry + 4);
			u32 off = rd_be32 (entry + 12);
			uint w = 1u << ((fmt_dims >> 4) & 0xf);
			uint h = 1u << (fmt_dims & 0xf);
			uint fmt = (fmt_dims >> 8) & 0xff;
			uint mips = fmt_dims >> 16;
			fprintf (f, "subtexture[%u] hash=0x%08x width=%u height=%u format=0x%02x mips=%u offset=0x%x\n",
				i, hash, w, h, fmt, mips, off);
		}
	}

	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (4) ".mat" material float-record table

int IsMercuryMat (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size != file_size)
		return 0;
	if (size == 0)
		return 1;
	if (size < 16)
		return 0;
	u32 count = rd_be32 (data);
	if (16 + (u64)count * 128 == size)
		return 1;
	count = rd_le32 (data);
	return 16 + (u64)count * 128 == size;
}

enumError DecodeMercuryMat_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!f || !data)
		return EINVAL;

	fprintf (f, "# Mercury Meltdown Revolution material table (.mat)\n");
	if (size == 0)
	{
		fprintf (f, "# empty material table\n");
		return ERR_OK;
	}
	if (size < 16)
		return EINVAL;

	u32 count = rd_be32 (data);
	bool le = false;
	if (16 + (u64)count * 128 != size)
	{
		count = rd_le32 (data);
		le = true;
	}

	fprintf (f, "material_count = %u%s\n", count, le ? "  # little-endian (PSP asset tool legacy)" : "");
	for (uint i = 0; i < count && 16 + (i + 1) * 128 <= size; i++)
	{
		const u8 *rec = data + 16 + i * 128;
		u32 hash = le ? rd_le32 (rec) : rd_be32 (rec);
		u32 flags = le ? rd_le32 (rec + 4) : rd_be32 (rec + 4);
		fprintf (f, "material[%u] hash=0x%08x flags=0x%08x\n", i, hash, flags);
	}
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (5) ".nav" navigation-mesh table

int IsMercuryNav (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size != file_size)
		return 0;
	if (size == 0)
		return 1;
	if (size < 8)
		return 0;
	u32 nodes = rd_le32 (data);
	u32 edges = rd_le32 (data + 4);
	return nodes < 10000 && edges < 10000;
}

enumError DecodeMercuryNav_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!f || !data)
		return EINVAL;

	fprintf (f, "# Mercury Meltdown Revolution navigation-mesh table (.nav)\n");
	if (!size)
	{
		fprintf (f, "# empty file -- a confirmed valid state for this extension\n");
		return ERR_OK;
	}
	if (size < 8)
		return EINVAL;

	u32 node_count = rd_le32 (data);
	u32 edge_count = rd_le32 (data + 4);
	fprintf (f, "node_count = %u\n", node_count);
	fprintf (f, "edge_count = %u\n", edge_count);
	return ERR_OK;
}
