// SPDX-License-Identifier: GPL-2.0+
// Split out of lib-nintendo-archives.c -- one archive format per file.
#include "lib-nintendo-archives.h"
#include "lib-nintendo.h"
#include "lib-image.h"
#include "lib-camelot.h"
#include "lib-yay0.h"
#include "lib-flim.h"
#include "lib-szs.h"
#include "lib-std.h"
#include "lib-zstd.h"
#include "lib-archive-util.h"
#include <zlib.h>
#include <stdlib.h>
#include <string.h>


// Extract Pokemon Mystery Dungeon Resource Container (.sir0 / SIR0)
enumError ExtractSIR0Archive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".sir0") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 16 || memcmp (raw, "SIR0", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	// SIR0 Header:
	// 0x00: "SIR0" (4 bytes)
	// 0x04: u32 SubHeaderOffset / DataPointer
	// 0x08: u32 PointerOffsetsOffset
	// 0x0C: u32 Magic / Padding (usually 0)
	const u32 subheader_offset = rd_le32 (raw + 4);
	const u32 pointer_offsets = rd_le32 (raw + 8);

	// Extract primary data segment (from 0x10 to subheader_offset)
	// and subheader segment (from subheader_offset to pointer_offsets)
	u32 data_end = (subheader_offset >= 0x10 && subheader_offset <= raw_size) ? subheader_offset
																			  : (u32)raw_size;
	u32 sub_end = (pointer_offsets >= data_end && pointer_offsets <= raw_size) ? pointer_offsets
																			   : (u32)raw_size;

	if (subheader_offset >= 0x10 && subheader_offset < raw_size && sub_end > subheader_offset)
	{
		char sub_file[PATH_MAX];
		snprintf (sub_file, sizeof (sub_file), "%s/subheader.bin", dest);
		if (!testmode)
			SaveFile (sub_file, 0, 0, raw + subheader_offset, sub_end - subheader_offset, 0);
	}

	char out_file[PATH_MAX];
	snprintf (out_file, sizeof (out_file), "%s/data.bin", dest);
	if (!testmode && data_end > 0x10)
		SaveFile (out_file, 0, 0, raw + 0x10, data_end - 0x10, 0);

	// The bytes from pointer_offsets to EOF are the SIR0 pointer-offset
	// table: a game-specific variable-length delta list of the pointer
	// locations inside data.bin that need relocation, followed by 0xAA
	// padding to the next 16-byte boundary. There is no generic way to
	// re-derive this table from data.bin alone, so it is saved verbatim
	// here and copied back unchanged by CreateSIR0Archive() -- that is
	// what makes a retail SIR0 file byte-for-byte reproducible.
	if (sub_end < raw_size)
	{
		char ptr_file[PATH_MAX];
		snprintf (ptr_file, sizeof (ptr_file), "%s/pointers.bin", dest);
		if (!testmode)
			SaveFile (ptr_file, 0, 0, raw + sub_end, raw_size - sub_end, 0);
	}

	FREE (raw);
	return ERR_OK;
}


// Pokemon Mystery Dungeon Resource Container (.sir0 / SIR0), little-endian
enumError CreateSIR0Archive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	const nintendo_sarc_entry_t *data_ent = 0;
	const nintendo_sarc_entry_t *sub_ent = 0;
	const nintendo_sarc_entry_t *ptr_ent = 0;

	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = leaf_name (entries[i].name);
		if (!strcasecmp (name, "data.bin") || !strcasecmp (name, "data"))
			data_ent = entries + i;
		else if (!strcasecmp (name, "subheader.bin") || !strcasecmp (name, "subheader"))
			sub_ent = entries + i;
		else if (!strcasecmp (name, "pointers.bin") || !strcasecmp (name, "pointers"))
			ptr_ent = entries + i;
	}

	if (!data_ent && n_entries >= 1)
		data_ent = entries;
	if (!sub_ent && n_entries >= 2 && entries + 1 != data_ent)
		sub_ent = entries + 1;

	const u32 data_sz = data_ent ? data_ent->size : 0;
	const u32 sub_sz = sub_ent ? sub_ent->size : 0;
	const u32 ptr_sz = ptr_ent ? ptr_ent->size : 16;

	const u32 sub_offset = 0x10 + data_sz;
	const u32 pointer_offsets = sub_offset + sub_sz;
	// pointers.bin holds the exact pointer-offset table bytes captured by
	// ExtractSIR0Archive(); writing it back verbatim (rather than
	// re-deriving it) is what makes a retail archive byte-for-byte
	// reproducible. Fall back to 16 zero bytes when absent (e.g. a
	// hand-built data.bin/subheader.bin pair with no captured table).
	const u32 total = pointer_offsets + ptr_sz;

	u8 *buf = CALLOC (total, 1);
	if (!buf)
		return ERR_OUT_OF_MEMORY;

	memcpy (buf, "SIR0", 4);
	wr_le32 (buf + 4, sub_offset);
	wr_le32 (buf + 8, pointer_offsets);
	wr_le32 (buf + 12, 0);

	if (data_ent && data_ent->data && data_sz)
		memcpy (buf + 0x10, data_ent->data, data_sz);
	if (sub_ent && sub_ent->data && sub_sz)
		memcpy (buf + sub_offset, sub_ent->data, sub_sz);
	if (ptr_ent && ptr_ent->data && ptr_sz)
		memcpy (buf + pointer_offsets, ptr_ent->data, ptr_sz);

	*dest = buf;
	*dest_size = total;
	return ERR_OK;
}
