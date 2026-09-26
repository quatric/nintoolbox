// SPDX-License-Identifier: GPL-2.0+
#include "lib-xtd.h"
#include "lib-nintendo.h"
#include "lib-std.h"
#include "lib-archive-util.h"
#include <ctype.h>
#include <string.h>

// ----------------------------------------------------------------------------
// Genki "GTI Club: Supermini Festa!" resource table (.unq.xtd)
// ----------------------------------------------------------------------------
// Header (all fields little-endian, unlike the rest of this engine's data):
//   +0x00  "XTD\0"
//   +0x04  4 ASCII digits + NUL, e.g. "003\0" (format version)
//   +0x08  u32  entry_count
//   +0x0C  u32[entry_count]  absolute file offset of each entry
//
// Each entry:
//   +0x00  u32  entry byte size (span from this offset to the next entry,
//               or to EOF for the last one)
//   +0x04  4 ASCII chars, NUL-padded: resource tag (only "TPL\0" observed)
//   +0x08  u16  unknown, always 0 in every sample seen
//   +0x0A  u16  entry index (equals the entry's position in the table)
//   +0x0C  u16  unknown, always 1
//   +0x0E  u16  unknown, always 1
//   +0x80  payload -- for tag "TPL\0" this is a bit-exact Nintendo TPL
//          file (magic 0x0020AF30), decodable as-is by wimgt.
// The 0x80 byte sub-header size is constant across every sample regardless
// of entry size or payload type.
enum
{
	XTD_ENTRY_HEADER_SIZE = 0x80,
	XTD_MAX_ENTRIES = 100000,
};

bool IsXTD (const u8 *data, uint size)
{
	if (!data || size < 12 || memcmp (data, "XTD\0", 4))
		return false;

	const u32 count = rd_le32 (data + 8);
	if (!count || count > XTD_MAX_ENTRIES || (u64)12 + (u64)count * 4 > size)
		return false;

	for (u32 i = 0; i < count; i++)
	{
		const u32 off = rd_le32 (data + 12 + i * 4);
		if ((u64)off + XTD_ENTRY_HEADER_SIZE > size)
			return false;
		const u32 entry_size = rd_le32 (data + off);
		if (entry_size < XTD_ENTRY_HEADER_SIZE || (u64)off + entry_size > size)
			return false;
	}
	return true;
}

enumError ExtractXTDBuffer (const u8 *data, uint size, ccp dest, bool print_msg, ccp src_name)
{
	if (!IsXTD (data, size))
		return ERR_NOTHING_TO_DO;
	const u32 count = rd_le32 (data + 8);

	if (!testmode)
		CreatePath (dest, true);

	if (print_msg && (verbose >= 0 || testmode))
		fprintf (stdlog, "%s%sEXTRACT XTD:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", src_name, count, dest);

	enumError err = ERR_OK;
	for (u32 i = 0; i < count; i++)
	{
		const u32 off = rd_le32 (data + 12 + i * 4);
		const u32 entry_size = rd_le32 (data + off);
		const u16 index = rd_le16 (data + off + 0xA);
		char tag[5];
		memcpy (tag, data + off + 4, 4);
		tag[4] = 0;
		for (int c = 0; c < 4; c++)
			if (!isalnum ((unsigned char)tag[c]))
				tag[c] = 0;

		const u32 payload_off = off + XTD_ENTRY_HEADER_SIZE;
		const u32 payload_size = entry_size - XTD_ENTRY_HEADER_SIZE;

		char out_path[PATH_MAX];
		if (!strcmp (tag, "TPL"))
			snprintf (out_path, sizeof (out_path), "%s/tex_%02u.tpl", dest, index);
		else
			snprintf (out_path, sizeof (out_path), "%s/entry_%02u_%s.bin", dest, index,
				tag[0] ? tag : "raw");

		if (!testmode)
		{
			err = SaveFile (out_path, 0, 0, data + payload_off, payload_size, 0);
			if (err)
				break;
		}
	}
	return err;
}

enumError ExtractXTDArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".xtd") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size > UINT_MAX || !IsXTD (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	err = ExtractXTDBuffer (raw, (uint)raw_size, dest, true, arg);

	FREE (raw);
	return err;
}
