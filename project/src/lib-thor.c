// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Behaviour Interactive "Thor" resource package (.wii)
// Phineas and Ferb: Quest for Cool Stuff (Wii)
//-----------------------------------------------------------------------------
//
// Confirmed by direct byte-level analysis of all 85 retail .wii files
// (DATA/files/phinferb/*/*.wii), ranging from 1536 bytes to 23 MiB.
//
// Layout:
//   0x00  8 bytes: fixed signature 01 00 00 00 00 00 00 02
//   0x08  u32 zero
//   0x0C  2x u16 (BE): package id / checksum pair, meaning unconfirmed
//   0x10  char name[12]      (NUL padded, e.g. "garage06")
//   0x1C  char category[12]  (NUL padded, e.g. "phinferb")
//   0x28  20 bytes zero padding
//   0x3C  4 bytes ASCII "Thor" -- build-tool/engine marker
//   0x40  a run of 8-byte BE (u32 key, u32 val) slots, each normally the
//         sentinel { 0xFFFFFFFF, 0x00000000 } (an oversized, mostly-empty
//         lookup/reservation table). The run ends with exactly two
//         non-sentinel slots back to back:
//           { 0xFFFFFFFF, count }       -- resource_count (count != 0)
//           { table_offset, 0 }         -- absolute file offset of the
//                                          real resource table
//   table_offset .. count * 12-byte BE records: { u32 id, u32 offset,
//         u32 size } describing each resource blob in the file. This
//         table was verified to be internally consistent (offsets
//         non-decreasing, offset+size within the file) on every one of
//         the 85 sample files.
//
// Some resource blobs additionally start with a small envelope:
//   +0   4 bytes 0xAB 0xAB 0xAB 0xAB (marker)
//   +4   u32 unknown
//   +8   u32 zero
//   +12  u32 id (repeats the table entry's id)
//   +16  NUL-terminated resource name (e.g. "garage06.lua")
// This was observed on compiled Lua script resources and on the fixed
// 96-byte pattern-fill records in memtest.wii/corpo.wii/stubs.wii, but
// not on every resource (larger binary blobs -- geometry/physics/etc. --
// have no such envelope and are dumped as opaque raw data). When present,
// it is used to give the extracted file a real name; otherwise the entry
// is named by table index.
//
// The "Thor" marker's real meaning (engine name vs. a person's name used
// by the build pipeline) was not confirmed -- main.dol string dumps only
// show generic paths like "z:\generated\thor\phineas_ferb\wii\..." and
// no class/engine names tied to it. It was adopted here only because it
// is the sole concrete, stable identifier embedded in every sample.
//-----------------------------------------------------------------------------

#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-thor.h"
#include "lib-archive-util.h"
#include <string.h>
#include <ctype.h>

enum
{
	THOR_HEADER_SIZE = 0x40,
	THOR_NAME_MAX = 200000, // sanity cap for extracted blob names
};

static const u8 thor_sig[8] = { 0x01, 0, 0, 0, 0, 0, 0, 0x02 };

bool IsThorPkg (const u8 *data, size_t size)
{
	if (!data || size < THOR_HEADER_SIZE)
		return false;
	if (memcmp (data, thor_sig, sizeof (thor_sig)))
		return false;
	if (memcmp (data + 0x3C, "Thor", 4))
		return false;
	return true;
}

static void copy_padded_name (char *dest, size_t dest_size, const u8 *src, size_t src_size)
{
	size_t n = src_size < dest_size - 1 ? src_size : dest_size - 1;
	memcpy (dest, src, n);
	dest[n] = 0;
	// truncate at the first embedded NUL
	char *nul = memchr (dest, 0, n);
	if (!nul)
		dest[n] = 0;
}

enumError ScanThorPkg (thor_t *thor, const u8 *data, size_t size)
{
	if (!thor || !data)
		return ERR_INVALID_DATA;
	memset (thor, 0, sizeof (*thor));

	if (!IsThorPkg (data, size))
		return ERR_INVALID_DATA;

	thor->raw = data;
	thor->raw_size = size;
	copy_padded_name (thor->name, sizeof (thor->name), data + 0x10, 12);
	copy_padded_name (thor->category, sizeof (thor->category), data + 0x1C, 12);

	// walk the mostly-empty 8-byte slot run to find the { 0xFFFFFFFF, count }
	// / { table_offset, 0 } marker pair.
	size_t off = THOR_HEADER_SIZE;
	u32 count = 0, table_off = 0;
	while (off + 16 <= size)
	{
		const u32 k1 = rd_be32 (data + off);
		const u32 v1 = rd_be32 (data + off + 4);
		const u32 k2 = rd_be32 (data + off + 8);
		const u32 v2 = rd_be32 (data + off + 12);
		if (k1 == 0xFFFFFFFF && v1 != 0 && v2 == 0 && k2 >= THOR_HEADER_SIZE && k2 < size)
		{
			count = v1;
			table_off = k2;
			break;
		}
		off += 8;
	}

	if (!table_off || !count || count > 1000000)
		return ERR_INVALID_DATA;

	const u64 table_end = (u64)table_off + (u64)count * 12;
	if (table_end > size)
		return ERR_INVALID_DATA;

	thor->entries = CALLOC (count, sizeof (thor_entry_t));
	if (!thor->entries)
		return ERR_CANT_CREATE;
	thor->table_offset = table_off;

	uint n = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *p = data + table_off + (u64)i * 12;
		const u32 id = rd_be32 (p);
		const u32 ofs = rd_be32 (p + 4);
		const u32 len = rd_be32 (p + 8);
		if (!len || (u64)ofs + len > size)
		{
			// stop at the first entry that doesn't fit rather than
			// failing the whole file -- keep whatever validated so far.
			break;
		}
		thor->entries[n].id = id;
		thor->entries[n].offset = ofs;
		thor->entries[n].size = len;
		n++;
	}
	thor->n_entries = n;

	if (!n)
	{
		ResetThorPkg (thor);
		return ERR_INVALID_DATA;
	}

	return ERR_OK;
}

void ResetThorPkg (thor_t *thor)
{
	if (!thor)
		return;
	if (thor->entries)
		FREE (thor->entries);
	memset (thor, 0, sizeof (*thor));
}

// Try to pull a real resource name out of the small envelope some blobs
// carry right at their start (see file header comment). Returns true and
// fills 'out' (sanitized, safe for a path component) if found.
//
// Two envelope variants are confirmed by byte evidence:
//   A) +0  4 bytes 0xAB 0xAB 0xAB 0xAB marker, +4 u32 unknown, +8 u32 zero,
//      +12 u32 id, +16 NUL-terminated name. Seen on compiled script/stub
//      resources.
//   B) +0  12 zero bytes, +12 u32 id, +16 NUL-terminated name (a short
//      logical resource name, e.g. "metal136", "flamefire02" -- texture
//      resources referenced from level/asset packages). Confirmed on
//      211/212 previously-unresolved fallback blobs across the sample
//      corpus (assets.wii and per-level texture tables): id field always
//      matches the owning table entry's id, and the following bytes are a
//      short printable name. Only the name is recovered here -- the pixel
//      data's own layout/format is not decoded (see file header comment).
static bool try_envelope_name (const u8 *blob, u32 blob_size, u32 id, char *out, size_t out_size)
{
	if (blob_size < 20)
		return false;

	bool variant_a = !memcmp (blob, "\xab\xab\xab\xab", 4);
	bool variant_b = !variant_a && !memcmp (blob, "\0\0\0\0\0\0\0\0\0\0\0\0", 12);
	if (!variant_a && !variant_b)
		return false;
	if (rd_be32 (blob + 12) != id)
		return false;

	const u8 *name = blob + 16;
	const u32 max_len = blob_size - 16 < 128 ? blob_size - 16 : 128;
	uint len = 0;
	while (len < max_len && name[len])
		len++;
	if (!len || len >= max_len)
		return false;

	for (uint i = 0; i < len; i++)
	{
		const u8 c = name[i];
		if (!isprint (c) || c == '/' || c == '\\')
			return false;
	}

	if (len >= out_size)
		len = out_size - 1;
	memcpy (out, name, len);
	out[len] = 0;
	return true;
}

enumError ExtractThorPkg (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".wii"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsThorPkg (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	thor_t thor;
	err = ScanThorPkg (&thor, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT THOR:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, thor.n_entries, dest);

	for (uint i = 0; i < thor.n_entries; i++)
	{
		const thor_entry_t *e = thor.entries + i;
		const u8 *blob = raw + e->offset;

		char envname[128];
		char out_path[PATH_MAX];
		if (try_envelope_name (blob, e->size, e->id, envname, sizeof (envname))
			&& OwnedNameOk (envname))
			snprintf (out_path, sizeof (out_path), "%s/%04u_%s", dest, i, envname);
		else
			snprintf (out_path, sizeof (out_path), "%s/%04u_%08x.bin", dest, i, e->id);

		if (!testmode)
			SaveFile (out_path, 0, 0, blob, e->size, 0);
	}

	ResetThorPkg (&thor);
	FREE (raw);
	return ERR_OK;
}
