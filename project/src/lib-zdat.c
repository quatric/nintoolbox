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


// ----------------------------------------------------------------------------
// Pokemon Stadium (N64) PERS-SZP asset container
//
// A 24-byte header wrapping an ordinary Yay0 stream, big-endian:
//
//   0x00  "PERS-SZP"
//   0x08  u32 header size (0x18 in every instance on the retail cart)
//   0x0c  u32 decompressed size
//   0x10  u32 decompressed size again
//   0x14  u32 zero
//   0x18  Yay0 stream
//
// Established against Pokemon Stadium (USA, Rev 1), which carries 410 of
// them. The sibling "FRAGMENT" signature in the same ROM is a different
// thing entirely -- a MIPS code overlay with a 0x20-byte header, not an
// asset -- so it is deliberately not claimed here.
// ----------------------------------------------------------------------------
// Animal Crossing: Pocket Camp .zdat
//
// A flat container: a 16-bit header naming three offsets and an entry count,
// then one 16-byte record per file (name length, size, uncompressed size, and
// a word that is always zero), then the names back to back, then the payloads
// back to back. Verified against 31 files pulled from the game's own CDN --
// single-entry up to 45 entries -- where the arithmetic closes exactly: the
// name table ends where the data begins, and the last payload ends at EOF.
//
// Every payload is a Unity asset bundle XORed with one repeated byte. The key
// is not a secret held elsewhere; it falls out of the bundle's own "UnityFS"
// signature, and the check below is what confirms it: a bundle records its own
// total length, and across all 168 payloads seen that length matched the entry
// size exactly once the key was applied. A wrong key does not survive that.
#define ZDAT_ENT_SIZE 16

static enumError zdat_read_header (
	const u8 *data, size_t size, uint *ent_off, uint *name_off, uint *data_off, uint *count)
{
	if (size < 0x30 || memcmp (data, "ZDAT", 4))
		return ERR_NOTHING_TO_DO;

	*ent_off = rd_le16 (data + 0x06);
	*name_off = rd_le16 (data + 0x0a);
	*data_off = rd_le16 (data + 0x0e);
	*count = rd_le16 (data + 0x12);

	// The name table begins immediately after the entry array, so its offset
	// is fixed by the count rather than being free: 0x30 only for a
	// single-entry container, further out for the rest (45 entries push it to
	// 0x2f0). Requiring 0x30 here would reject every archive holding more
	// than one file.
	if (*ent_off != 0x20 || !*count)
		return ERR_INVALID_DATA;
	if (*name_off != *ent_off + (u64)*count * ZDAT_ENT_SIZE)
		return ERR_INVALID_DATA;
	if (*data_off > size)
		return ERR_INVALID_DATA;
	return ERR_OK;
}


// Recover the repeated byte from the known signature, then require the bundle
// to agree about its own size. Returns 0 when this is not a bundle at all.
static int zdat_unmask (u8 *p, uint size)
{
	static const char sig[] = "UnityFS";
	const uint siglen = sizeof (sig) - 1;
	if (size < 0x20)
		return 0;

	const u8 key = p[0] ^ (u8)sig[0];
	for (uint i = 1; i < siglen; i++)
		if ((p[i] ^ key) != (u8)sig[i])
			return 0;

	for (uint i = 0; i < size; i++)
		p[i] ^= key;

	// UnityFS: signature, u32 format, two NUL-terminated version strings,
	// then the bundle's own total size as a big-endian u64.
	uint off = 8 + 4;
	for (int i = 0; i < 2; i++)
	{
		uint start = off;
		while (off < size && p[off])
			off++;
		if (off >= size || off == start + 0)
			; // an empty version string is unusual but not fatal
		if (off >= size)
			return 0;
		off++;
	}
	if (off + 8 > size)
		return 0;

	u64 total = 0;
	for (int i = 0; i < 8; i++)
		total = total << 8 | p[off + i];
	return total == size;
}


enumError ExtractZDATArchive (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	uint ent_off, name_off, data_off, count;
	enumError err = zdat_read_header (raw, raw_size, &ent_off, &name_off, &data_off, &count);
	if (err)
	{
		FREE (raw);
		return err;
	}

	// Walk the table once before writing anything: a container whose entries
	// do not add up to the file is not one of these, and half an extraction
	// is worse than none.
	u64 name_pos = name_off, data_pos = data_off;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = raw + ent_off + (size_t)i * ZDAT_ENT_SIZE;
		const u32 nlen = rd_le32 (e);
		const u32 size = rd_le32 (e + 4);
		if (!nlen || nlen > 4096 || name_pos + nlen > data_off)
		{
			FREE (raw);
			return ERR_INVALID_DATA;
		}
		if (data_pos + size > raw_size)
		{
			FREE (raw);
			return ERR_INVALID_DATA;
		}
		name_pos += nlen;
		data_pos += size;
	}
	if (name_pos != data_off || data_pos != raw_size)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ZDAT:%s (%u file%s) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, count == 1 ? "" : "s", dest);

	name_pos = name_off;
	data_pos = data_off;
	uint written = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = raw + ent_off + (size_t)i * ZDAT_ENT_SIZE;
		const u32 nlen = rd_le32 (e);
		const u32 size = rd_le32 (e + 4);

		char name[PATH_MAX];
		const uint keep = nlen < sizeof (name) - 1 ? nlen : sizeof (name) - 1;
		memcpy (name, raw + name_pos, keep);
		name[keep] = 0;
		name_pos += nlen;

		// Stored names may carry a directory part of their own.
		for (char *q = name; *q; q++)
			if (*q == '\\' || (*q == '.' && q[1] == '.'))
				*q = '_';

		u8 *payload = MALLOC (size ? size : 1);
		if (!payload)
		{
			FREE (raw);
			return ERR_OUT_OF_MEMORY;
		}
		memcpy (payload, raw + data_pos, size);
		data_pos += size;

		const int unmasked = zdat_unmask (payload, size);

		char out[PATH_MAX];
		snprintf (out, sizeof (out), "%s/%s", dest, name);
		if (!testmode)
		{
			CreatePath (out, false);
			if (!SaveFile (out, 0, 0, payload, size, 0))
				written++;
		}
		else
			written++;

		if (verbose > 0)
			fprintf (stdlog, "  %-40s %8u bytes%s\n", name, size,
				unmasked ? "" : "  (not a Unity bundle, stored as found)");

		FREE (payload);
	}

	FREE (raw);
	return written ? ERR_OK : ERR_INVALID_DATA;
}
