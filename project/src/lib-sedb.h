// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Square Enix "SEDB" resource containers (Dragon Quest X, Wii: Data/bg/... and
// the layout/bin/wii/ trees of the S4MJ update titles). Reverse engineered.
//
// Every SEDB file starts with a header, all fields little-endian even though
// the Wii payload that follows is big-endian:
//   0x00 char[4] "SEDB"
//   0x04 char[4] resource type, NUL padded ("RES ", "txb", "shd", "PHB", "lyb")
//   0x08 u32 version
//   0x0c u8 / u8 flags, u16 header_size (0x20; 0x30 for "lyb")
//   0x10 u32 file size
//   0x18 u32 build time (unix)
//
// RES (.rps) is the index container. After its 0x20 byte header:
//   0x20 u32 n_records
//   0x24 u32 (offset hint, unused)
//   0x28 u32 n_records - 1
//   0x2c char[4] own resource type, byte reversed ("spr" = "rps")
//   0x30 n_records * { u32 index, u32 offset, u32 size, u32 1 }
// Record offsets are relative to the end of the table (0x30 + 16 * n_records).
// Records are SEDB sub-files except the last three metadata records:
//   n-3 .. : string pools / padding (not exported)
//   n-2    : type table (unused: each member header carries its own type)
//   n-1    : name table, n * char[16], NUL padded resource names
// Record i is named by name[i]; the extension comes from its SEDB header.
//
// shd: after the 0x20 SEDB header and a 0x20 "SHD" header come chunks of
// { char[4] tag, u32 0, u32 size, ... } ("WMAT" material flags, "PRAM", ...).
// PRAM (all big-endian): at chunk+0x18 u32 n, then 5 section words, n record
// offsets and n name offsets, all relative to chunk+0x10. A record is 0x20
// bytes: u8 flag, u8 1, u8 component count, 13 more bytes, then f32[count] at
// +0x10. Names are NUL terminated strings (diffuseColor, fogParam, ...).
//
// lyb is the big-endian Lay editor object database (object proxies, type
// names, resource paths); only its header and string pool are decoded.
//
// PHB starts with a "PHB.GBD" block (+0x40: f32[4] min, f32[4] max bounding
// box) followed by "PHB.TBC" / "PHB.TBD" block pairs holding bounding boxes and
// 16-bit quantized collision data; only the block count and box are decoded.
//
// txb holds one texture: a 0x20 SEDB header, then a big-endian DDS-style
// header (0x80 bytes: size, flags, height, width, ..., mip count at +0x1c,
// pixel format at +0x4c) and the GX pixel data (base level first, then mips):
//   fourcc "DXT1" (stored reversed)      -> GX CMPR   (8x8 tiles of 4 blocks)
//   pf flags 0x20001, 16 bpp             -> linear u8 luminance, u8 alpha
//   pf flags 0x41,    32 bpp             -> linear R,G,B,A bytes
//   pf flags 0x20,     8 bpp             -> 256 * A,R,G,B palette, then linear indices
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_SEDB_H
#define SZS_LIB_SEDB_H 1

#include "lib-nintendo.h"

// True for any SEDB container (checks "SEDB" and a plausible header).
bool IsSEDB (const u8 *data, uint size);

// True for a SEDB RES index container.
bool IsSEDBRes (const u8 *data, uint size);

// True for a SEDB texture ("txb").
bool IsSEDBTexture (const u8 *data, uint size);

// Split a RES container into its sub-files, named "<name>.<type>". The entry
// list is malloc-owned and holds copies of the payloads.
enumError ScanSEDBRes (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

// Decode the base level of a txb texture into a malloc'ed, tightly packed
// RGBA8 buffer. Returns ERR_NOTHING_TO_DO for pixel formats that are not
// supported.
enumError DecodeSEDBTexture (u8 **rgba, uint *width, uint *height, const u8 *data, uint size);

// Human readable summary of a non-texture SEDB member ("shd" material with its
// named float parameters, "lyb" layout object database with its string pool,
// "PHB" collision blob with its bounding box and block count). Returns a
// malloc'ed text, or NULL for other types.
char *DumpSEDBText (const u8 *data, uint size);

#endif
