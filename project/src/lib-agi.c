// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Toys for Bob "AGI" archive scanner; see lib-agi.h for the layout and the
// documented limitation of the entry-table heuristic.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-agi.h"
#include <string.h>

#define AGI_MAGIC        0x1A414749
#define AGI_HEADER_SIZE  0x40
#define AGI_MAX_ENTRIES  0x100000

static u32 agi_rd32 (const u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

enumError ScanAGI (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size)
{
	if (!entries || !n_entries || !data || size < AGI_HEADER_SIZE || agi_rd32 (data) != AGI_MAGIC)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u32 entry_table_size  = agi_rd32 (data + 0x08);
	const u32 name_count        = agi_rd32 (data + 0x0c);
	const u32 name_table_offset = agi_rd32 (data + 0x2c);
	const u32 name_table_size   = agi_rd32 (data + 0x30);

	if (!name_count || name_count > AGI_MAX_ENTRIES)
		return EINVAL;
	if ((u64)AGI_HEADER_SIZE + entry_table_size > size)
		return EINVAL;
	// Verified invariant on every sample: the name table runs to EOF exactly.
	if (name_table_offset < AGI_HEADER_SIZE || (u64)name_table_offset + name_table_size != size)
		return EINVAL;

	// Scan the entry table for adjacent big-endian u32 words (offset, size)
	// with a plausible data range. This covers pure-audio-bank .pak files,
	// whose entry records really are 2-word (offset, size) pairs.
	const u8 *tbl = data + AGI_HEADER_SIZE;
	const uint n_words = entry_table_size / 4;
	struct pair_t { u32 off, size; } *pairs = CALLOC (name_count + 1, sizeof (*pairs));
	if (!pairs)
		return ERR_CANT_CREATE;
	uint n_pairs = 0;
	for (uint i = 0; i + 1 < n_words && n_pairs < name_count + 1; i++)
	{
		const u32 off = agi_rd32 (tbl + i * 4);
		const u32 msz = agi_rd32 (tbl + i * 4 + 4);
		if (off >= AGI_HEADER_SIZE && off < name_table_offset && (u64)off + msz <= size)
		{
			pairs[n_pairs].off  = off;
			pairs[n_pairs].size = msz;
			n_pairs++;
			i++; // consume both words of the pair
		}
	}

	// Files that mix .igz/.igx model/material blobs alongside (or instead
	// of) audio use a different, 4-word entry record instead:
	//   word0: 0 (reserved, always zero in every sample seen)
	//   word1: block-aligned (AGI_BLOCK_ALIGN, 0x800) absolute file offset
	//          of the member's raw data
	//   word2: for text/binary members this is the exact byte length; for
	//          members whose payload looks compressed (opaque high-entropy
	//          bytes, mostly .igz) it does NOT match the on-disk size -- it
	//          appears to be a decompressed/logical size instead, so it is
	//          NOT used to bound the raw extraction
	//   word3: a per-entry flag (0xffffffff for small stub-like entries,
	//          0x2000000x otherwise; purpose beyond that is unclear)
	// Entries appear in the same order as the name table. Since word2 can't
	// be trusted for the on-disk length, the raw byte range of each member
	// is derived the same way FSB pairs are trusted here: from the *next*
	// distinct block-aligned offset in ascending order (or name_table_offset
	// for the highest-offset member). Verified against 780 real mixed .pak
	// files: every resulting byte range starts with the expected member
	// signature (0x5d00 at byte offset 2 for .igz/.igx "AGI object" blobs,
	// or an ASCII '<' for the handful of plain-XML .igx stubs) 100% of the
	// time -- see the scanner test notes in lib-agi.h.
	if (n_pairs != name_count)
	{
		n_pairs = 0;
		enum { AGI_BLOCK_ALIGN = 0x800 };
		u32 *offs = CALLOC (name_count, sizeof (*offs));
		if (!offs)
		{
			FREE (pairs);
			return ERR_CANT_CREATE;
		}
		for (uint start = 0; start + 4 * name_count <= n_words && n_pairs != name_count; start++)
		{
			uint cnt = 0, i = start;
			while (cnt < name_count && i + 4 <= n_words)
			{
				const u32 w0 = agi_rd32 (tbl + i * 4);
				const u32 w1 = agi_rd32 (tbl + i * 4 + 4);
				if (w0 || w1 < AGI_HEADER_SIZE || w1 >= name_table_offset || w1 % AGI_BLOCK_ALIGN)
					break;
				offs[cnt++] = w1;
				i += 4;
			}
			if (cnt == name_count)
				n_pairs = cnt;
		}
		if (n_pairs == name_count)
		{
			// Sort a copy of the offsets to find each member's successor.
			u32 *sorted = CALLOC (name_count, sizeof (*sorted));
			if (!sorted)
			{
				FREE (offs);
				FREE (pairs);
				return ERR_CANT_CREATE;
			}
			memcpy (sorted, offs, name_count * sizeof (*sorted));
			for (uint i = 1; i < name_count; i++)
			{
				u32 key = sorted[i];
				int j = (int)i - 1;
				while (j >= 0 && sorted[j] > key)
				{
					sorted[j + 1] = sorted[j];
					j--;
				}
				sorted[j + 1] = key;
			}
			for (uint i = 0; i < name_count; i++)
			{
				u32 next = name_table_offset;
				for (uint j = 0; j < name_count; j++)
					if (sorted[j] > offs[i] && sorted[j] < next)
						next = sorted[j];
				pairs[i].off  = offs[i];
				pairs[i].size = next - offs[i];
			}
			FREE (sorted);
		}
		FREE (offs);
	}

	// Bail out cleanly (no partial/garbled extraction) when neither layout
	// covers this file -- see the documented limitation in lib-agi.h.
	if (n_pairs != name_count)
	{
		FREE (pairs);
		return EINVAL;
	}

	// Name table: name_count big-endian u32 offsets (relative to the name
	// table start) into a block of NUL-terminated backslash-path strings,
	// each followed by 4 bytes of ignored hash/CRC.
	const u8 *nt = data + name_table_offset;
	if ((u64)name_count * 4 > name_table_size)
	{
		FREE (pairs);
		return EINVAL;
	}

	nintendo_sarc_entry_t *out = CALLOC (name_count, sizeof (*out));
	if (!out)
	{
		FREE (pairs);
		return ERR_CANT_CREATE;
	}

	uint n = 0;
	for (uint i = 0; i < name_count; i++)
	{
		const u32 str_off = agi_rd32 (nt + i * 4);
		if (str_off >= name_table_size)
			continue;
		const u8 *str = nt + str_off;
		const u8 *end = memchr (str, 0, name_table_size - str_off);
		if (!end)
			continue;

		// Use the basename of the last backslash component as the output
		// filename, mirroring how other archive libs here name members.
		ccp path = (ccp)str;
		ccp base = strrchr (path, '\\');
		base = base ? base + 1 : path;

		char name[512];
		if (!*base || strlen (base) >= sizeof (name) || !OwnedNameOk (base))
			snprintf (name, sizeof (name), "%05u.bin", i);
		else
			strcpy (name, base);

		// Audio bank members carry no extension in the AGI name table.
		// Tag raw FSB5 blobs with ".fsb" so the extractor's normal
		// recursive pass (extract_tree_complete()) auto-decodes them via
		// the existing FSB4/FSB5 decoder (lib-fsb.c / ScanFSB()).
		if (pairs[i].size >= 4 && !memcmp (data + pairs[i].off, "FSB5", 4)
			&& (strlen (name) < 4 || strcasecmp (name + strlen (name) - 4, ".fsb")))
		{
			const size_t len = strlen (name);
			if (len + 4 < sizeof (name))
				strcpy (name + len, ".fsb");
		}

		if (!OwnedEntryAdd (out, n, name, data + pairs[i].off, pairs[i].size))
		{
			ResetOwnedEntries (out, n);
			FREE (pairs);
			return ERR_CANT_CREATE;
		}
		n++;
	}
	FREE (pairs);

	if (!n)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}
