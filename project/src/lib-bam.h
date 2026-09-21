// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Puzzle level grid data (.bam, "Bust-A-Move BASH!", Wii). No text magic;
// gated on a fixed 4x u16 little-endian header and an exact-size invariant.
//
// Layout, all fields little-endian:
//   0x00  u16 field0   (2 in every one of 501 samples)
//   0x02  u16 field1   (22 in every one of 501 samples; meaning not recovered)
//   0x04  u16 width
//   0x06  u16 height
//   0x08  width*height bytes: the bubble grid, row-major, one byte per cell
//         (a per-cell bubble-color/type index; 0 reads as empty in the
//         smaller levels)
//   0x08 + width*height : u16 sentinel, always 0xffff
//
// Verified across all 501 pzl/*.bam samples on the disc: field0 and field1
// are constant, and `8 + width*height + 2 == file size` holds exactly on
// 500 of the 501 files. The single exception (E008.bam) has the same
// width/height as several 90-byte files but is 114 bytes long: grid and
// sentinel decode correctly at the expected offsets, and the 24 trailing
// bytes (22 zero bytes then a second 0xffff) are stale leftover data past
// the real end of the record, not part of the format -- carved out
// untouched as a trailing.bin sidecar rather than guessed at.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_BAM_H
#define SZS_LIB_BAM_H 1

#include "lib-std.h"

enumError ExtractBamArchive (ccp arg, ccp basedir, uint depth);

#endif
