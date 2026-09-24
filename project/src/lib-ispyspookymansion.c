// SPDX-License-Identifier: GPL-2.0+
// "I Spy Spooky Mansion" (Wii) formats -- see lib-ispyspookymansion.h for
// exactly what is and is not understood about each of them.

#include "lib-ispyspookymansion.h"
#include "lib-nintendo.h"
#include <string.h>
#include <ctype.h>
#include <math.h>

//-----------------------------------------------------------------------------
// shared helpers

static int is_printable_name (const u8 *data, size_t size, u32 ofs)
{
	if (ofs >= size)
		return 0;
	size_t j = ofs;
	int len = 0;
	while (j < size && data[j])
	{
		if (data[j] < 0x20 || data[j] >= 0x7f)
			return 0;
		j++;
		len++;
		if (len > 4096)
			return 0;
	}
	return j < size; // must be NUL-terminated in-bounds
}

static void print_cstr (FILE *f, const u8 *data, size_t size, u32 ofs)
{
	if (ofs >= size)
		return;
	size_t j = ofs;
	while (j < size && data[j])
		j++;
	fwrite (data + ofs, 1, j - ofs, f);
}

//-----------------------------------------------------------------------------
// (1) ".eid" sound-event/effect table -- shared "VARS" field-record helper

enum
{
	EID_EFF_ENTRY_SIZE = 20,
	EID_VARS_HEADER_SIZE = 12,
	EID_FIELD_RECORD_SIZE = 16,
};

// Decodes one "VARS"-tagged field-record table at absolute offset [vars_ofs].
// Shared between .eid effect blocks and .sdf TYPE-record payloads, since
// both use the exact same layout (confirmed byte-for-byte identical).
static enumError decode_vars_block (FILE *f, const u8 *data, size_t size, u32 vars_ofs, int indent)
{
	if ((u64) vars_ofs + EID_VARS_HEADER_SIZE > size || memcmp (data + vars_ofs, "VARS", 4))
	{
		fprintf (f, "%*s# 'VARS' block at 0x%x invalid/out-of-bounds -- not decoded\n", indent, "", vars_ofs);
		return ERR_OK;
	}

	u32 field_count = rd_be32 (data + vars_ofs + 4);
	u32 ro = vars_ofs + EID_VARS_HEADER_SIZE;

	fprintf (f, "%*sfield_count = %u\n", indent, "", field_count);

	for (u32 i = 0; i < field_count; i++)
	{
		if ((u64) ro + EID_FIELD_RECORD_SIZE > size)
		{
			fprintf (f, "%*s# field table truncated -- not decoded further\n", indent, "");
			break;
		}

		u32 name_ofs = rd_be32 (data + ro);
		u32 type     = rd_be32 (data + ro + 4);
		const u8 *tag = data + ro + 8;
		u32 value    = rd_be32 (data + ro + 12);
		float valuef;
		memcpy (&valuef, &value, 4);

		fprintf (f, "%*sfield[%u]: name=\"", indent, "", i);
		if (is_printable_name (data, size, name_ofs))
			print_cstr (f, data, size, name_ofs);
		else
			fprintf (f, "?");
		fprintf (f, "\" type=0x%x tag=\"", type);
		for (int k = 0; k < 4; k++)
		{
			u8 c = tag[k];
			if (c >= 0x20 && c < 0x7f)
				fputc (c, f);
		}
		fprintf (f, "\" value=0x%x (as_float=%g)", value, valuef);

		// OGGFILE-style fields store a second string offset in `value`
		// instead of a number; print it when it resolves to one.
		if (is_printable_name (data, size, value) && value != 0)
		{
			fprintf (f, " (as_string=\"");
			print_cstr (f, data, size, value);
			fprintf (f, "\")");
		}
		fprintf (f, "\n");

		ro += EID_FIELD_RECORD_SIZE;
	}

	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (1) ".eid" sound-event/effect table

int IsSpookyEid (const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!data || size < 0x14)
		return 0;
	if (memcmp (data, "EID\0", 4))
		return 0;
	if (rd_be32 (data + 4) != 1 || rd_be32 (data + 8) != 0 || rd_be32 (data + 0xc) != 0)
		return 0;

	u32 count = rd_be32 (data + 0x10);
	if ((u64) 0x14 + (u64) count * EID_EFF_ENTRY_SIZE > size)
		return 0;

	// Validate the effect-table entries structurally so magic-only false
	// positives (and the one known byte-swapped outlier sample) are
	// rejected rather than mis-decoded.
	u32 o = 0x14;
	for (u32 i = 0; i < count; i++)
	{
		if (memcmp (data + o, "EFF\0", 4) || memcmp (data + o + 8, "OGG\0", 4))
			return 0;
		u32 vars_ofs = rd_be32 (data + o + 12);
		if ((u64) vars_ofs + EID_VARS_HEADER_SIZE > size || memcmp (data + vars_ofs, "VARS", 4))
			return 0;
		o += EID_EFF_ENTRY_SIZE;
	}
	return 1;
}

enumError DecodeSpookyEid_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsSpookyEid (data, size, file_size))
		return EINVAL;

	u32 count = rd_be32 (data + 0x10);
	fprintf (f, "# I Spy Spooky Mansion sound-event/effect table (.eid)\n");
	fprintf (f, "effect_count = %u\n", count);

	u32 o = 0x14;
	for (u32 i = 0; i < count; i++)
	{
		u32 string_tab_ofs = rd_be32 (data + o + 4);
		u32 vars_ofs        = rd_be32 (data + o + 12);
		fprintf (f, "\neffect[%u]: string_table_ofs=0x%x vars_ofs=0x%x\n", i, string_tab_ofs, vars_ofs);
		decode_vars_block (f, data, size, vars_ofs, 2);
		o += EID_EFF_ENTRY_SIZE;
	}
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (2) ".ast" "SDASSETF" asset-bundle container

enum { AST_HEADER_SIZE = 16, AST_CHUNK_HEADER_SIZE = 16 };

// Returns 1 and sets *swapped if the 8-byte magic matches "SDASSETF"
// either directly (big-endian samples) or word-swapped (little-endian
// samples exported by the "Proteus" authoring tool). See lib-ispyspooky
// mansion.h note (2).
static int check_ast_magic (const u8 *data, size_t size, int *swapped)
{
	if (!data || size < AST_HEADER_SIZE)
		return 0;
	if (!memcmp (data, "SDASSETF", 8))
	{
		*swapped = 0;
		return 1;
	}
	u8 rev[8];
	for (int i = 0; i < 4; i++)
		rev[i] = data[3 - i];
	for (int i = 0; i < 4; i++)
		rev[4 + i] = data[7 - i];
	if (!memcmp (rev, "SDASSETF", 8))
	{
		*swapped = 1;
		return 1;
	}
	return 0;
}

static u32 ast_u32 (const u8 *data, u32 ofs, int swapped)
{
	return swapped ? rd_le32 (data + ofs) : rd_be32 (data + ofs);
}

static void ast_tag (const u8 *data, u32 ofs, int swapped, char out[5])
{
	if (swapped)
	{
		out[0] = data[ofs + 3];
		out[1] = data[ofs + 2];
		out[2] = data[ofs + 1];
		out[3] = data[ofs + 0];
	}
	else
		memcpy (out, data + ofs, 4);
	out[4] = 0;
}

static int is_ascii_tag (const char tag[5])
{
	int any = 0;
	for (int i = 0; i < 4; i++)
	{
		u8 c = (u8) tag[i];
		if (c && (c < 0x20 || c >= 0x7f))
			return 0;
		if (c)
			any = 1;
	}
	return any;
}

int IsSpookyAst (const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	int swapped;
	if (!check_ast_magic (data, size, &swapped))
		return 0;

	// Require the first top-level chunk header to look structurally valid
	// (printable tag, in-bounds size, zero reserved field) so arbitrary
	// data that happens to start with the magic isn't accepted.
	if (size < AST_HEADER_SIZE + AST_CHUNK_HEADER_SIZE)
		return 0;
	char tag[5];
	ast_tag (data, AST_HEADER_SIZE, swapped, tag);
	if (!is_ascii_tag (tag))
		return 0;
	u32 chunk_size = ast_u32 (data, AST_HEADER_SIZE + 8, swapped);
	if ((u64) AST_HEADER_SIZE + AST_CHUNK_HEADER_SIZE + chunk_size > size)
		return 0;
	return 1;
}

enumError DecodeSpookyAst_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsSpookyAst (data, size, file_size))
		return EINVAL;

	int swapped;
	check_ast_magic (data, size, &swapped);
	u32 version = ast_u32 (data, 8, swapped);
	u32 count   = ast_u32 (data, 12, swapped);

	fprintf (f, "# I Spy Spooky Mansion \"SDASSETF\" asset-bundle container (.ast)\n");
	fprintf (f, "byte_order = %s\n", swapped ? "little-endian (word-swapped)" : "big-endian");
	fprintf (f, "version = %u\n", version);
	fprintf (f, "chunk_count = %u  # top-level chunk count (declared)\n", count);

	u32 o = AST_HEADER_SIZE;
	u32 n = 0;
	while (o + AST_CHUNK_HEADER_SIZE <= size)
	{
		char tag[5];
		ast_tag (data, o, swapped, tag);
		if (!is_ascii_tag (tag))
			break;
		u32 cver      = ast_u32 (data, o + 4, swapped);
		u32 chunksize = ast_u32 (data, o + 8, swapped);
		u32 field4    = ast_u32 (data, o + 12, swapped);
		if ((u64) o + AST_CHUNK_HEADER_SIZE + chunksize > size)
		{
			fprintf (f, "\n# chunk table stops at offset 0x%x (out-of-bounds size field) --\n"
				"# remainder not decoded, see lib-ispyspookymansion.h note (2)\n", o);
			break;
		}

		fprintf (f, "\nchunk[%u]: tag=\"%s\" version=0x%x size=%u field4=0x%x offset=0x%x\n",
			n, tag, cver, chunksize, field4, o);

		u32 end = o + AST_CHUNK_HEADER_SIZE + chunksize;
		u32 next = (end + 15) & ~15u;
		if (next <= o || next > size)
		{
			fprintf (f, "# nested/sub-chunk payload not reverse-engineered -- see lib-ispyspookymansion.h note (2)\n");
			break;
		}
		o = next;
		n++;
	}

	if (n < count)
		fprintf (f, "\n# only %u of %u declared top-level chunks could be walked; the rest\n"
			"# (or this chunk's nested payload) was not reverse-engineered\n", n, count);

	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (3) ".sdf" asset-type registry table

enum { SDF_HEADER_SIZE = 16, SDF_TYPE_RECORD_SIZE = 12 };

int IsSpookySdf (const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!data || size < SDF_HEADER_SIZE)
		return 0;
	if (memcmp (data, "SDF\0", 4))
		return 0;
	if (rd_be32 (data + 4) != 1 || rd_be32 (data + 8) != 0)
		return 0;

	u32 count = rd_be32 (data + 0xc);
	if ((u64) SDF_HEADER_SIZE + (u64) count * SDF_TYPE_RECORD_SIZE > size)
		return 0;

	u32 o = SDF_HEADER_SIZE;
	for (u32 i = 0; i < count; i++)
	{
		if (memcmp (data + o, "TYPE", 4))
			return 0;
		o += SDF_TYPE_RECORD_SIZE;
	}
	return 1;
}

enumError DecodeSpookySdf_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsSpookySdf (data, size, file_size))
		return EINVAL;

	u32 count = rd_be32 (data + 0xc);
	fprintf (f, "# I Spy Spooky Mansion asset-type registry table (.sdf)\n");
	fprintf (f, "type_count = %u\n", count);

	u32 o = SDF_HEADER_SIZE;
	u32 body_ofs = SDF_HEADER_SIZE + count * SDF_TYPE_RECORD_SIZE;
	for (u32 i = 0; i < count; i++)
	{
		char type_tag[5];
		memcpy (type_tag, data + o + 4, 4);
		type_tag[4] = 0;
		u32 len = rd_be32 (data + o + 8);

		fprintf (f, "\ntype[%u]: tag=\"%s\" length=%u body_ofs=0x%x\n", i, type_tag, len, body_ofs);
		if ((u64) body_ofs + EID_VARS_HEADER_SIZE <= size && !memcmp (data + body_ofs, "VARS", 4))
			decode_vars_block (f, data, size, body_ofs, 2);
		else
			fprintf (f, "  # type body is not a 'VARS' record table -- not decoded\n");

		body_ofs += len;
		o += SDF_TYPE_RECORD_SIZE;
	}
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (4) ".ges" Wiimote gesture recording

enum { GES_SIZE = 136, GES_POINT_COUNT = 10 };

int IsSpookyGes (const u8 *data, size_t size, size_t file_size)
{
	(void) file_size;
	if (!data || size != GES_SIZE)
		return 0;
	if (rd_be32 (data) != GES_POINT_COUNT)
		return 0;
	if (rd_be32 (data + 4) != 1 || rd_be32 (data + 8) != 1 || rd_be32 (data + 0xc) != 1)
		return 0;
	return 1;
}

enumError DecodeSpookyGes_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsSpookyGes (data, size, file_size))
		return EINVAL;

	fprintf (f, "# I Spy Spooky Mansion Wiimote gesture recording (.ges)\n");
	fprintf (f, "point_count = %u\n", GES_POINT_COUNT);
	fprintf (f, "dim_x = 1\ndim_y = 1\ndim_z = 1\n\n");

	for (u32 i = 0; i < GES_POINT_COUNT; i++)
	{
		u32 ofs = 0x10 + i * 12;
		u32 xb = rd_be32 (data + ofs), yb = rd_be32 (data + ofs + 4), zb = rd_be32 (data + ofs + 8);
		float x, y, z;
		memcpy (&x, &xb, 4);
		memcpy (&y, &yb, 4);
		memcpy (&z, &zb, 4);
		fprintf (f, "point[%u] = (%g, %g, %g)\n", i, x, y, z);
	}
	return ERR_OK;
}
