// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Metroid Prime Remastered (Switch) PACK container.
//
// Little-endian RFRM form (id "PACK", v1) holding a "TOCC" v3 table of
// contents with three chunk kinds: "ADIR" (u32 count + 52-byte asset
// entries: FourCC type, 16-byte LE uuid, u32 version/other_version,
// u64 absolute offset/decompressed-size/size), "META" (u32 count +
// per-asset {uuid, u32 offset} records pointing at u32-prefixed blobs
// inside the chunk body) and "STRG" (u32 count + {byteswapped FourCC,
// uuid, u32 name length, name} records). Each asset's bytes are a raw
// RFRM resource (type id + versions must match the directory, and
// decompressed-size must equal the inner form size + 32), stored raw or
// compressed (u32 LE mode 0 = stored, 1..3 = LZSS, same group math as
// the Tropical Freeze LZSS stream but with a 4-byte LE mode word).
//
// Layout per PrimeDecomp/retrotool's lib/format/pack.rs (MIT/Apache-2.0;
// re-implemented here, not copied) and verified byte-for-byte against
// the retail Preload/MPR1/GameplayOverrides.pak (448 bytes: LDTA +
// DGRP assets, STRG names "CameraOverrides" / "PreloadResources_DGRP").
//
// Extracted assets are written with a FOOT form appended (AINF asset id
// + compression mode + original offset, META blob, NAME strings), the
// same convention retrotool uses, so its `pak package` accepts our
// output and our own TXTR decoder finds the META it needs.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_MPR_PAK_H
#define SZS_LIB_MPR_PAK_H 1

#include "types.h"

#define MPR_PACK_MAX_ENTRIES 1000000
#define MPR_PACK_MAX_OUTPUT (512u << 20)

// One ADIR asset record. STRG names / META blob pointers reference
// malloc'd pak-owned storage (see mpr_pack_t); TYPE/GUID/VERSIONS/
// OFFSET/SIZES are plain values, safe to copy.
typedef struct mpr_pack_entry_t
{
	char type[4]; // FourCC resource type, e.g. "TXTR"
	u8 guid[16]; // LE-ordered uuid bytes as stored
	u32 version;
	u32 other_version;
	u64 offset; // absolute file offset of the (possibly compressed) bytes
	u64 decomp_size;
	u64 size; // stored size; size != decomp_size means LZSS mode 1..3
} mpr_pack_entry_t;

typedef struct mpr_pack_name_t
{
	u8 guid[16];
	char *name; // owned, NUL-terminated
} mpr_pack_name_t;

typedef struct mpr_pack_meta_t
{
	u8 guid[16];
	u8 *data; // owned blob (size-prefixed word stripped)
	uint size;
} mpr_pack_meta_t;

typedef struct mpr_pack_t
{
	const u8 *data; // source buffer (borrowed, must outlive the pak)
	uint size;
	mpr_pack_entry_t *entries; // owned, n_entries (consecutive dup guids collapsed)
	uint n_entries;
	mpr_pack_name_t *names; // owned STRG table (all names, in file order)
	uint n_names;
	mpr_pack_meta_t *metas; // owned META table
	uint n_metas;
} mpr_pack_t;

void ResetMPRPACK (mpr_pack_t *pak);

// Lightweight probe: full ScanMPRPACK with all storage discarded.
// Used by DetectNintendoFormat(); extraction calls ScanMPRPACK directly.
bool IsMPRPACK (const u8 *data, uint size);
// Full structural scan: PACK v1 / TOCC v3, every ADIR entry
// bounds-checked with its inner RFRM header (id + versions +
// decomp_size == form size + 32) verified. No decompression happens
// here, so multi-GB paks scan cheaply. Fails cleanly (never partial)
// on anything that is not this container.
enumError ScanMPRPACK (mpr_pack_t *pak, const u8 *data, uint size);

// First STRG name for GUID, or NULL. The pak keeps ownership.
ccp FindMPRPACKName (const mpr_pack_t *pak, const u8 guid[16]);

// META blob for GUID, or NULL (size via META_SIZE). Borrowed.
const u8 *FindMPRPACKMeta (const mpr_pack_t *pak, const u8 guid[16], uint *meta_size);

// Decompress one entry into a fresh malloc'd buffer (*DEST_SIZE bytes).
// Mode comes from the stored stream (u32 LE 0..3); mode 0 requires an
// exact size match (stored). Fails cleanly on truncated input.
enumError GetMPRPACKEntry (u8 **dest, uint *dest_size, const mpr_pack_t *pak, uint index);

// Format a stored LE uuid as canonical 8-4-4-4-12 hex into OUT (36+1).
// Matches retrotool's display, hence its extracted filenames.
void FormatMPRGUID (char out[37], const u8 guid[16]);

// Build the FOOT form retrotool appends on extract (AINF + META + NAMEs)
// into a fresh malloc'd buffer. META/NAMES come from the pak tables;
// pass entry index I. NAMES_OUT receives the name count used.
enumError BuildMPRPACKFoot (u8 **dest, uint *dest_size, const mpr_pack_t *pak, uint index,
	uint comp_mode, u64 orig_offset);

// Raw LZSS payload decoder (u32 LE mode word + stream) shared by the
// entry path above. Exposed for unit tests.
enumError DecodeMPR_LZSS (u8 **dest, uint *dest_size, const u8 *src, uint src_size,
	uint decomp_size);

#endif
