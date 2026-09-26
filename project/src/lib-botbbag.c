// SPDX-License-Identifier: GPL-2.0+
// "Battle of the Bands" (Wii) ".bag" asset container -- see lib-botbbag.h
// for exactly what is and is not understood about it.

#include "lib-botbbag.h"
#include "dclib-debug.h"
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <zlib.h>

//-----------------------------------------------------------------------------
// constants

#define BAG_HDR_SIZE 32
#define BAG_MAX_NAME 64
#define BAG_MAX_ENTRIES 4096
#define BAG_INFLATE_CAP (64u * 1024u * 1024u) // sanity cap per blob

//-----------------------------------------------------------------------------
// header parsing

// Parses the fixed 32-byte "1.00 <N>\n\0\0..." header. Returns 1 and fills
// *n on success, else 0.
static int parse_header (const u8 *data, size_t size, u64 *n)
{
	if (!data || size < BAG_HDR_SIZE)
		return 0;
	if (memcmp (data, "1.00 ", 5) != 0)
		return 0;

	size_t nl = 5;
	while (nl < BAG_HDR_SIZE && data[nl] != '\n')
		nl++;
	if (nl >= BAG_HDR_SIZE || nl == 5)
		return 0; // no newline found within the field, or empty number

	u64 val = 0;
	for (size_t i = 5; i < nl; i++)
	{
		if (data[i] < '0' || data[i] > '9')
			return 0;
		val = val * 10 + (u64)(data[i] - '0');
	}

	for (size_t i = nl + 1; i < BAG_HDR_SIZE; i++)
		if (data[i])
			return 0; // rest of the fixed field must be zero-padded

	*n = val;
	return 1;
}

int IsBotbBag (const u8 *data, size_t size, size_t file_size)
{
	if (file_size < BAG_HDR_SIZE || size < BAG_HDR_SIZE)
		return 0;

	u64 n;
	if (!parse_header (data, size, &n))
		return 0;

	// N is some (title-specific, not fully understood) sub-length of the
	// payload -- sanity-gate it against the payload instead of requiring
	// exact equality, see lib-botbbag.h.
	u64 payload_len = file_size - BAG_HDR_SIZE;
	if (!n || n > payload_len)
		return 0;

	return 1;
}

//-----------------------------------------------------------------------------
// manifest record extraction: "<name>,<size>,<offset>\n" CSV lines
// embedded (bracketed by runs of '*') in the payload.

typedef struct bag_entry_t
{
	char name[BAG_MAX_NAME];
	u64 csize; // "size" field as declared in the manifest
	u64 offset; // absolute file offset

} bag_entry_t;

// Tries to parse one manifest record starting at data[pos]. On success,
// fills *ent and returns the number of bytes consumed (up to and
// including the trailing '\n'); returns 0 on no match.
static size_t try_entry (const u8 *data, size_t size, size_t pos, bag_entry_t *ent)
{
	size_t p = pos, name_start = pos;
	while (p < size && p - name_start < BAG_MAX_NAME - 1
		&& (isalnum (data[p]) || data[p] == '_' || data[p] == '.' || data[p] == '-'))
		p++;
	size_t name_len = p - name_start;
	if (!name_len || p >= size || data[p] != ',')
		return 0;
	// require at least one letter, so we don't match stray numeric noise
	int has_alpha = 0;
	for (size_t i = name_start; i < p; i++)
		if (isalpha (data[i]))
		{
			has_alpha = 1;
			break;
		}
	if (!has_alpha)
		return 0;
	p++; // skip ','

	u64 csize = 0;
	size_t d0 = p;
	while (p < size && isdigit (data[p]) && p - d0 < 12)
		csize = csize * 10 + (u64)(data[p++] - '0');
	if (p == d0 || p >= size || data[p] != ',')
		return 0;
	p++; // skip ','

	u64 off = 0;
	size_t d1 = p;
	while (p < size && isdigit (data[p]) && p - d1 < 12)
		off = off * 10 + (u64)(data[p++] - '0');
	if (p == d1 || p >= size || data[p] != '\n')
		return 0;
	p++; // skip '\n'

	memcpy (ent->name, data + name_start, name_len);
	ent->name[name_len] = 0;
	ent->csize = csize;
	ent->offset = off;
	return p - pos;
}

//-----------------------------------------------------------------------------
// zlib probe: does a valid zlib stream begin at file offset 'off'?

static int try_inflate (const u8 *data, size_t size, u64 off, size_t *out_dec_size)
{
	if (off + 2 > size || data[off] != 0x78
		|| data[off + 1] != 0x9c && data[off + 1] != 0x5e && data[off + 1] != 0x01
			&& data[off + 1] != 0xda)
		return 0;

	z_stream zs;
	memset (&zs, 0, sizeof zs);
	if (inflateInit (&zs) != Z_OK)
		return 0;

	zs.next_in = (Bytef *)(data + off);
	zs.avail_in = (uInt)(size - off);

	size_t cap = 65536, total = 0;
	u8 *buf = MALLOC (cap);
	int ok = 0, ret;
	do
	{
		if (total == cap)
		{
			if (cap >= BAG_INFLATE_CAP)
				break;
			cap *= 2;
			buf = REALLOC (buf, cap);
		}
		zs.next_out = buf + total;
		zs.avail_out = (uInt)(cap - total);
		ret = inflate (&zs, Z_NO_FLUSH);
		total = cap - zs.avail_out;
		if (ret == Z_STREAM_END)
		{
			ok = 1;
			break;
		}
	} while (ret == Z_OK && zs.avail_in);

	inflateEnd (&zs);
	FREE (buf);
	if (ok && out_dec_size)
		*out_dec_size = total;
	return ok;
}

//-----------------------------------------------------------------------------
// text dump

enumError DecodeBotbBag_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || size < BAG_HDR_SIZE)
		return EINVAL;

	u64 n = 0;
	int hdr_ok = parse_header (data, size, &n);

	fprintf (f, "# Battle of the Bands (Wii) .bag asset container\n");
	fprintf (f, "file_size = %zu\n", file_size);
	fprintf (f, "header_ok = %d\n", hdr_ok);
	if (hdr_ok)
	{
		fprintf (f, "header_n  = %llu\n", (unsigned long long)n);
		fprintf (f, "payload_len = %zu\n", size - BAG_HDR_SIZE);
	}

	fprintf (f, "\n# Recovered manifest (name,size,offset) records\n");
	int n_entries = 0, n_zlib = 0;
	size_t pos = BAG_HDR_SIZE;
	while (pos < size)
	{
		bag_entry_t ent;
		size_t consumed = try_entry (data, size, pos, &ent);
		if (!consumed)
		{
			pos++;
			continue;
		}

		int is_zlib = 0;
		size_t dec_size = 0;
		if (ent.offset < size)
			is_zlib = try_inflate (data, size, ent.offset, &dec_size);

		fprintf (f, "%-40s size=%-10llu offset=0x%06llx%s\n", ent.name,
			(unsigned long long)ent.csize, (unsigned long long)ent.offset, is_zlib ? "" : "");
		if (is_zlib)
			fprintf (f, "    zlib: inflates OK, decompressed_size=%zu\n", dec_size);

		if (is_zlib)
			n_zlib++;
		n_entries++;
		if (n_entries >= BAG_MAX_ENTRIES)
			break;
		pos += consumed;
	}
	fprintf (f, "# %d manifest record(s), %d confirmed zlib blob(s)\n", n_entries, n_zlib);

	return ERR_OK;
}
