// SPDX-License-Identifier: GPL-2.0+
// Donkey Konga (GameCube) proprietary asset formats. No public documentation
// of this format exists. Reverse-engineered from scratch against all three
// retail discs (Donkey Konga [USA], Donkey Konga 2 [USA], Donkey Konga 3 -
// Tabehoudai! Haru Mogitate 50-kyoku [Japan]), cross-checking every real
// sample byte-for-byte.
//
// ".tpl.dkz" -- "DKZF" trivial zlib texture wrapper. Donkey Konga (disc 1)
// only: every one of 64 real `files/**/*.tpl.dkz` samples on that disc
// decodes cleanly. Donkey Konga 2 and 3 ship their textures as plain `.nut`
// files instead and carry no `.dkz` files at all, so this wrapper is
// confirmed disc-1-only, not merely untested elsewhere. Layout, all fields
// big-endian:
//   char magic[4];         // "DKZF"
//   u32  decompressed_size;
//   u8   zlib_stream[];    // standard raw zlib stream (0x78 0x?? magic)
// Inflating the stream reproduces the wrapped file exactly (confirmed:
// decompressed size matches the header field and the output always starts
// with the Nintendo TPL magic `00 20 AF 30` in every one of the 64 samples
// -- this disc only ever wraps `.tpl` textures). The wrapper carries no name
// of its own; the double extension on disk ("Foo.tpl.dkz") is what tells
// you what is inside, so decoding just strips the ".dkz" suffix and hands
// the revealed plain ".tpl" file back to this repo's ordinary file-type
// detection/decode path.
#ifndef SZS_LIB_DONKEYKONGA_H
#define SZS_LIB_DONKEYKONGA_H 1

#include "lib-nintendo.h"

//-----------------------------------------------------------------------------
// ".tpl.dkz" "DKZF" zlib texture wrapper

bool IsDKZF (const u8 *data, size_t size);

// Inflate D's wrapped payload (malloc-owned via *dest). *dest_size receives
// the decompressed size, which is cross-checked against the header field.
enumError DecodeDKZF (const u8 *data, size_t size, u8 **dest, uint *dest_size);

#endif // SZS_LIB_DONKEYKONGA_H
