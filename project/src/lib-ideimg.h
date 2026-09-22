// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Bully: Scholarship Edition (Wii) "IDE.dir"/"IDE.img" object-definition
// archive pair (DATA/files/Objects). Classic GTA-era .dir/.img archive
// format (same sector-table scheme as GTA III/Vice City's GTA3.dir/img on
// PC/PS2) -- no magic, just a flat table of fixed 32-byte entries in the
// ".dir" file describing 2048-byte-sector ranges into the sibling ".img":
//
//   per entry (32 bytes, big-endian, "IDE.dir"):
//     +0x00  u32 start_sector  (byte offset into .img = start_sector*2048)
//     +0x04  u32 num_sectors   (byte length = num_sectors*2048, rounded up
//            to the sector; the real member is usually a little shorter,
//            see below)
//     +0x08  24 bytes: NUL-terminated ASCII member name (e.g. "default.idb"),
//            zero-padded
//
// Verified against the sole real sample on disc (Objects/IDE.dir, 77
// entries, 2464 = 77*32 bytes): entries are contiguous and in order
// (entry[n].start_sector == entry[n-1].start_sector + entry[n-1].num_sectors
// for all 77), and the last entry's end (247 sectors * 2048 =
// 505856 bytes) is byte-exact against the real Objects/IDE.img size.
//
// Each ".idb" member itself is a plain-text, human-readable GTA-style
// "objs/tobj/path/2dfx" IDE definition block (same syntax as the sibling
// plain-text .ide files elsewhere in DATA/files/Objects) -- trailing sector
// padding inside a member (usually NUL bytes) is left in the extracted
// file rather than guessed at, since the .dir table gives no separate
// "exact byte length" field to trim to.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_IDEIMG_H
#define SZS_LIB_IDEIMG_H 1

#include "lib-std.h"

enumError ExtractIdeImgArchive (ccp arg, ccp basedir, uint depth);

#endif
