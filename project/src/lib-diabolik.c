// SPDX-License-Identifier: GPL-2.0+
// "Diabolik: The Original Sin" (Wii) tagged-block resource container -- see
// lib-diabolik.h for exactly what is and is not understood about it.

#include "lib-diabolik.h"
#include "lib-nintendo.h"
#include <string.h>

//-----------------------------------------------------------------------------
// constants

#define DBK_MAGIC 0xfaaffaafu
#define DBK_EOF_TAG 0xfeeffeefu
#define DBK_TAG_SECTION 0xbbbbbbbbu
#define DBK_TAG_LEAF 0xbebebebeu

#define DBK_MAX_DEPTH 32 // sanity cap against pathological nesting
#define DBK_MAX_NAME_LEN                                                                           \
	64 // ASCII name/id fields are 20 bytes in every
	   // sample seen; allow generous headroom
#define DBK_MAX_TEXT_CHARS                                                                         \
	8192 // UTF-16BE dialogue lines; longest real
		 // sample seen is well under this
#define DBK_BLOB_MIN                                                                               \
	4096 // LEAF payloads at/above this size are
		 // reported as candidate embedded audio

static int is_block_tag (u32 tag)
{
	return tag == DBK_TAG_SECTION || tag == DBK_TAG_LEAF;
}

//-----------------------------------------------------------------------------
// detection

int IsDiabolikRes (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < 20 || file_size < 20)
		return 0;
	if (rd_be32 (data) != DBK_MAGIC)
		return 0;
	if (rd_be32 (data + 4) != 8)
		return 0;

	u32 tag = rd_be32 (data + 8);
	if (!is_block_tag (tag))
		return 0;

	u32 blk_size = rd_be32 (data + 12);
	if (blk_size < 8 || (u64)8 + blk_size != file_size)
		return 0; // the outer block always spans exactly to EOF

	if (size < file_size)
		return 1; // header confirmed; not enough buffered to check the EOF tag
	return rd_be32 (data + size - 4) == DBK_EOF_TAG;
}

//-----------------------------------------------------------------------------
// string extraction

// buf[off..off+4) as a BE u32 length prefix -> a NUL-padded printable-ASCII
// field, if that's what's there. Returns the field's total byte length
// (4 + len, NOT including trailing pad past the printed text) or 0.
static u32 try_ascii_field (
	const u8 *data, size_t size, size_t off, const char **text, u32 *text_len)
{
	if (off + 4 > size)
		return 0;
	u32 len = rd_be32 (data + off);
	if (!len || len > DBK_MAX_NAME_LEN || off + 4 + len > size)
		return 0;

	const u8 *bytes = data + off + 4;
	u32 nul = len;
	for (u32 i = 0; i < len; i++)
		if (!bytes[i])
		{
			nul = i;
			break;
		}
	if (!nul)
		return 0; // an all-zero (empty) field carries no readable content
	for (u32 i = 0; i < nul; i++)
		if (bytes[i] < 0x20 || bytes[i] >= 0x7f)
			return 0;
	for (u32 i = nul; i < len; i++)
		if (bytes[i])
			return 0; // padding after the NUL must be all-zero

	*text = (const char *)bytes;
	*text_len = nul;
	return 4 + len;
}

// Encode one UTF-16BE code unit (BMP only, matches every sample seen) as
// UTF-8 into out[]; returns the number of bytes written (1-3), or 0 if out
// is too small.
static int utf8_put (char *out, size_t out_room, u16 cp)
{
	if (cp < 0x80)
	{
		if (out_room < 1)
			return 0;
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800)
	{
		if (out_room < 2)
			return 0;
		out[0] = (char)(0xc0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3f));
		return 2;
	}
	if (out_room < 3)
		return 0;
	out[0] = (char)(0xe0 | (cp >> 12));
	out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
	out[2] = (char)(0x80 | (cp & 0x3f));
	return 3;
}

// buf[off..off+4) as a BE u32 char-count prefix -> a plausible UTF-16BE
// text field (localized dialogue/subtitle strings). On success, fills
// utf8_out (caller-owned buffer) and returns the field's total byte length
// (4 + 2*nchars), else returns 0.
static u32 try_utf16_field (
	const u8 *data, size_t size, size_t off, char *utf8_out, size_t utf8_room)
{
	if (off + 4 > size)
		return 0;
	u32 nchars = rd_be32 (data + off);
	if (!nchars || nchars > DBK_MAX_TEXT_CHARS)
		return 0;
	size_t start = off + 4;
	size_t nbytes = (size_t)nchars * 2;
	if (start + nbytes > size)
		return 0;

	size_t out_pos = 0;
	int saw_alnum = 0;
	u32 trailing_nul = 0;
	for (u32 i = 0; i < nchars; i++)
	{
		u16 cp = rd_be16 (data + start + i * 2);
		if (!cp)
		{
			trailing_nul++;
			continue;
		}
		if (trailing_nul) // embedded NUL before the end -- not plain text
			return 0;
		if (cp < 0x20 || cp > 0x24ff)
			return 0;
		if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'))
			saw_alnum = 1;
		int n = utf8_put (utf8_out + out_pos, utf8_room - out_pos, cp);
		if (!n)
			return 0; // caller buffer too small; treat as non-match
		out_pos += (size_t)n;
	}
	if (!saw_alnum || out_pos >= utf8_room)
		return 0;
	utf8_out[out_pos] = 0;
	return 4 + (u32)nbytes;
}

// Single linear pass over the whole file printing every recovered ASCII
// name/id field and every recovered UTF-16BE dialogue/subtitle line, in
// file order. Deliberately not tied to the block tree walk below (a
// per-block scan would be quadratic on deeply-nested files); a length
// prefix only ever matches one encoding at a real field boundary in every
// sample checked.
static void dump_strings (FILE *f, const u8 *data, size_t size)
{
	char utf8_buf[3 * DBK_MAX_TEXT_CHARS + 1];
	int n_ascii = 0, n_text = 0;

	fprintf (f, "\n# Recovered strings (ASCII names/ids, UTF-16BE dialogue/subtitle text)\n");
	for (size_t off = 0; off + 4 <= size; off += 4)
	{
		const char *text;
		u32 text_len;
		u32 consumed = try_ascii_field (data, size, off, &text, &text_len);
		if (consumed)
		{
			fprintf (f, "ascii  @0x%06zx (%u) \"%.*s\"\n", off, text_len, (int)text_len, text);
			n_ascii++;
			continue;
		}
		consumed = try_utf16_field (data, size, off, utf8_buf, sizeof utf8_buf);
		if (consumed)
		{
			fprintf (f, "text   @0x%06zx \"%s\"\n", off, utf8_buf);
			n_text++;
		}
	}
	fprintf (f, "# %d ascii field(s), %d text line(s)\n", n_ascii, n_text);
}

//-----------------------------------------------------------------------------
// block tree walk

static void walk_block (
	FILE *f, const u8 *data, size_t off, size_t end, int depth, u32 *n_blocks, u32 *n_blobs)
{
	if (depth > DBK_MAX_DEPTH)
	{
		fprintf (f, "%*s... max nesting depth reached, stopping descent\n", depth * 2, "");
		return;
	}

	size_t pos = off;
	while (pos + 8 <= end)
	{
		u32 tag = rd_be32 (data + pos);
		if (!is_block_tag (tag))
			break; // not aligned on a block boundary -- rest is raw payload

		u32 blk_size = rd_be32 (data + pos + 4);
		if (blk_size < 8 || pos + blk_size > end)
			break;

		size_t payload_off = pos + 8;
		size_t payload_end = pos + blk_size;
		u32 payload_len = (u32)(payload_end - payload_off);

		fprintf (f, "%*s%-7s @0x%06zx size=%-8u payload=0x%06zx..0x%06zx (%u bytes)\n", depth * 2,
			"", tag == DBK_TAG_SECTION ? "SECTION" : "LEAF", pos, blk_size, payload_off,
			payload_end, payload_len);
		(*n_blocks)++;

		if (tag == DBK_TAG_LEAF && payload_len >= DBK_BLOB_MIN)
		{
			fprintf (f, "%*s  # candidate embedded blob (audio?), %u bytes\n", depth * 2, "",
				payload_len);
			(*n_blobs)++;
		}

		walk_block (f, data, payload_off, payload_end, depth + 1, n_blocks, n_blobs);
		pos = payload_end;
	}
}

//-----------------------------------------------------------------------------
// text dump

enumError DecodeDiabolikRes_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || size < 20)
		return EINVAL;

	fprintf (f, "# Diabolik: The Original Sin -- FAAFFAAF tagged-block resource container\n");
	fprintf (f, "# (.cfg / .gam / .loc / .ls / .rgn all share this layout; see lib-diabolik.h)\n");
	fprintf (f, "file_size = %zu\n", file_size);
	fprintf (f, "eof_tag_ok = %d\n", size >= 4 && rd_be32 (data + size - 4) == DBK_EOF_TAG);

	fprintf (f, "\n# Block tree\n");
	u32 n_blocks = 0, n_blobs = 0;
	walk_block (f, data, 8, size, 0, &n_blocks, &n_blobs);
	fprintf (f, "# %u block(s) total, %u candidate blob(s) >= %u bytes\n", n_blocks, n_blobs,
		DBK_BLOB_MIN);

	dump_strings (f, data, size);
	return ERR_OK;
}
