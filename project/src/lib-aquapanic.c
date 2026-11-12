// SPDX-License-Identifier: GPL-2.0+
// Eko Software Wii engine formats (Aqua Panic!, SPRay, Cyberbike, ...) -- see lib-aquapanic.h for the
// layouts and how they were checked.

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
		else if (c == '"')
			fputs ("\\\"", f);
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

// True if the whole file is in the buffer, so a walk can be required to end exactly at its end.
static bool have_all (size_t size, size_t file_size)
{
	return file_size && size >= file_size;
}

//-----------------------------------------------------------------------------
// (1) ".rck" / ".spa" "RKET" resource container

#define RKET_HEADER_SIZE 56

int IsAquaPanicRket (const u8 *data, size_t size, size_t file_size)
{
	if (file_size && size > file_size)
		size = file_size; // bytes past the end of the file are not part of it
	if (!data || size < RKET_HEADER_SIZE || (file_size && file_size < RKET_HEADER_SIZE))
		return 0;
	if (memcmp (data, "RKET", 4) || rd_le32 (data + 4))
		return 0;
	static const u8 fill[8] = { 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf };
	return !memcmp (data + 0x30, fill, sizeof (fill));
}

enumError DecodeAquaPanicRket_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsAquaPanicRket (data, size, file_size))
		return EINVAL;

	const u32 payload_size = rd_le32 (data + 20);
	fprintf (f, "# Eko Software RKET resource container (.rck / .spa)\n");
	fprintf (f, "hash = 0x%08x\n", rd_le32 (data + 8));
	fprintf (f, "version = 0x%04x\n", rd_le16 (data + 12));
	fprintf (f, "flags = 0x%04x\n", rd_le16 (data + 14));
	fprintf (f, "kind = %u\n", rd_le32 (data + 0x24));
	fprintf (f, "header_size = %u\n", RKET_HEADER_SIZE);
	fprintf (f, "payload_size = %u  # %s\n", payload_size,
		file_size == RKET_HEADER_SIZE + (u64)payload_size ? "stored (header_size + payload_size == file_size)"
														   : "unpacked size of a packed payload");
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (2) ".mat" "MATF" material chunk table

static bool is_chunk_tag (const u8 *p)
{
	for (int i = 0; i < 4; i++)
		if (p[i] != ' ' && !isupper (p[i]) && !isdigit (p[i]))
			return false;
	return true;
}

int IsAquaPanicMat (const u8 *data, size_t size, size_t file_size)
{
	if (file_size && size > file_size)
		size = file_size; // bytes past the end of the file are not part of it
	if (!data || size < 24 || file_size < 24)
		return 0;
	if (memcmp (data, "MATF", 4) || rd_le32 (data + 4) != 8)
		return 0;

	// Walk the chunk table as far as it is buffered; with the whole file it must end exactly at the end.
	u64 off = 16;
	uint n = 0;
	while (off + 8 <= size)
	{
		if (!is_chunk_tag (data + off))
			return 0;
		off += 8 + (u64)rd_le32 (data + off + 4);
		if (off > file_size)
			return 0;
		n++;
	}
	return n && (!have_all (size, file_size) || off == file_size);
}

enumError DecodeAquaPanicMat_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsAquaPanicMat (data, size, file_size))
		return EINVAL;

	fprintf (f, "# Eko Software MATF material chunk table (.mat)\n");
	fprintf (f, "count_a = %u\n", rd_le32 (data + 8));
	fprintf (f, "count_b = %u\n", rd_le32 (data + 12));
	fprintf (f, "# Chunk table (tag, payload_size); chunk payloads are not decoded.\n");

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
	if (file_size && size > file_size)
		size = file_size; // bytes past the end of the file are not part of it
	if (!data || size < 17 || file_size < 17)
		return 0;
	if (memcmp (data, "BNAM", 4) || rd_le32 (data + 4) != file_size - 8 || rd_le32 (data + 12) != 1
		|| data[16])
		return 0;

	// Names are length-prefixed and NUL-terminated; with the whole file they must end exactly at its end.
	u64 off = 17;
	while (off + 4 <= size)
	{
		const u32 len = rd_le32 (data + off);
		if (!len || len > 1024 || off + 4 + len > file_size)
			return 0;
		if (off + 4 + len <= size && data[off + 4 + len - 1])
			return 0;
		off += 4 + (u64)len;
	}
	return !have_all (size, file_size) || off == file_size;
}

enumError DecodeAquaPanicMb2_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsAquaPanicMb2 (data, size, file_size))
		return EINVAL;

	fprintf (f, "# Eko Software BNAM name-string table (.mb2)\n");
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
// (4) ".vis" visibility lists (no magic)

#define VIS_MAX_GROUPS 256
#define VIS_MAX_COUNT 0x10000

// Walk all blocks; returns the number of blocks, or 0 if the data is not a .vis file ending exactly at SIZE.
static uint vis_walk (const u8 *data, size_t size)
{
	u64 off = 0;
	uint blocks = 0;
	while (off < size)
	{
		if (off + 8 > size)
			return 0;
		const u32 n_groups = rd_le32 (data + off);
		const u32 total = rd_le32 (data + off + 4);
		if (!n_groups || n_groups > VIS_MAX_GROUPS || total > VIS_MAX_COUNT)
			return 0;
		off += 8;
		u64 sum = 0;
		for (u32 g = 0; g < n_groups; g++)
		{
			if (off + 8 > size)
				return 0;
			const u32 count = rd_le32 (data + off + 4);
			if (count > VIS_MAX_COUNT)
				return 0;
			off += 8 + 8 * (u64)count;
			sum += count;
			if (off > size)
				return 0;
		}
		if (sum != total)
			return 0;
		blocks++;
	}
	return blocks;
}

int IsAquaPanicVis (const u8 *data, size_t size, size_t file_size)
{
	if (file_size && size > file_size)
		size = file_size; // bytes past the end of the file are not part of it
	if (!data || size < 24 || !have_all (size, file_size) || file_size % 8)
		return 0;
	return vis_walk (data, file_size) > 0;
}

enumError DecodeAquaPanicVis_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsAquaPanicVis (data, size, file_size))
		return EINVAL;

	fprintf (f, "# Eko Software visibility lists (.vis)\n");
	fprintf (f, "objects = %u\n", vis_walk (data, file_size));
	fprintf (f, "# object[i]: group g: object/value pairs\n");

	size_t off = 0;
	for (uint obj = 0; off < file_size; obj++)
	{
		const u32 n_groups = rd_le32 (data + off);
		fprintf (f, "object[%u] entries=%u\n", obj, rd_le32 (data + off + 4));
		off += 8;
		for (u32 g = 0; g < n_groups; g++)
		{
			const u32 group = rd_le32 (data + off), count = rd_le32 (data + off + 4);
			off += 8;
			fprintf (f, "  group %u:", group);
			for (u32 k = 0; k < count; k++, off += 8)
				fprintf (f, " %u/%u", rd_le32 (data + off), rd_le32 (data + off + 4));
			fputc ('\n', f);
		}
	}
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (5) ".lit" light list

#define LIT_TAG 0x20031126
#define LIT_RECORD 36

int IsAquaPanicLit (const u8 *data, size_t size, size_t file_size)
{
	if (file_size && size > file_size)
		size = file_size; // bytes past the end of the file are not part of it
	if (!data || size < 12 || file_size < 12 || rd_le32 (data) != LIT_TAG)
		return 0;
	const u32 count = rd_le32 (data + 4);
	if (count > 0x10000 || file_size != 12 + (u64)LIT_RECORD * count)
		return 0;
	for (u32 i = 0; i < count && 8 + (u64)LIT_RECORD * (i + 1) <= size; i++)
	{
		const u8 *r = data + 8 + LIT_RECORD * i;
		if (r[3] != 0x01 || rd_le32 (r + 4))
			return 0;
		for (int c = 0; c < 3; c++)
		{
			const float v = rd_le_float (r + 20 + 4 * c);
			if (!(v >= 0.0f && v <= 256.0f))
				return 0;
		}
	}
	return !have_all (size, file_size) || !rd_le32 (data + file_size - 4);
}

enumError DecodeAquaPanicLit_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsAquaPanicLit (data, size, file_size))
		return EINVAL;

	const u32 count = rd_le32 (data + 4);
	fprintf (f, "# Eko Software light list (.lit)\n");
	fprintf (f, "date_tag = 0x%08x\n", rd_le32 (data));
	fprintf (f, "count = %u\n", count);
	for (u32 i = 0; i < count && 8 + (u64)LIT_RECORD * (i + 1) <= size; i++)
	{
		const u8 *r = data + 8 + LIT_RECORD * i;
		fprintf (f, "light[%u] id=0x%08x pos=(%g, %g, %g) color=(%g, %g, %g) param=%g\n", i, rd_le32 (r),
			rd_le_float (r + 8), rd_le_float (r + 12), rd_le_float (r + 16), rd_le_float (r + 20),
			rd_le_float (r + 24), rd_le_float (r + 28), rd_le_float (r + 32));
	}
	return ERR_OK;
}
