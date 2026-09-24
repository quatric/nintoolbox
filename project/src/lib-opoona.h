// SPDX-License-Identifier: GPL-2.0+
// Opoona (Wii, ArtePiazza) character animation formats: the .mol per-character
// manifest and the .mot skeletal-animation clip.
//
// No public documentation of these formats exists anywhere (XeNTaX, GBAtemp,
// Models-Resource and romhacking.net were all checked and are empty on this
// title). Everything below was reverse-engineered from scratch against the
// retail USA disc's own character/ files, cross-checked across dozens of
// files and several different characters, and partially confirmed against
// the game's own executable (main.dol) via static analysis: FUN_800625ac
// implements exactly the (offset,size,name) linear-scan lookup described
// below for .mol.
//
// What is understood and implemented here:
//   - .mol: a flat table of 32-byte records (offset, size, name[16], 8
//     reserved bytes), little-endian offset/size fields despite the rest of
//     the disc's data being big-endian, with an explicit ASCII "NULL"
//     placeholder name for unused slots. Confirmed byte-exact against the
//     real per-character .mot file sizes on disk.
//   - .mot: a ~0x70-byte big-endian header (clip duration in frames, a
//     constant 30.0 frame-rate float, and a bone/track-count+1 field --
//     the last confirmed via a 600-file statistical pass across many
//     characters, not merely inferred from a single sample) followed by a
//     flat array of 80-byte bone bind-pose records (scale xyz, 40 bytes
//     reserved, translate xyz, unit quaternion xyzw), each cross-validated
//     via quaternion-magnitude checks across multiple characters.
//
// What is explicitly NOT understood or decoded: for animated clips, the
// bone array is followed by a pool of real per-bone keyframe-curve data
// (confirmed to exist and to decode to physically plausible values by hand,
// but its indexing/layout -- which bytes belong to which bone, and how the
// header's second table pointer locates them -- was not pinned down despite
// extensive black-box and static-analysis effort). This module does not
// attempt to decode that pool; it is reported as an unparsed byte range,
// never guessed at.
#ifndef LIB_OPOONA_H
#define LIB_OPOONA_H 1

#include "lib-std.h"

//-----------------------------------------------------------------------------
// .mol manifest: no magic bytes. Structural probe validates the record
// table shape (see lib-opoona.c for the exact layout).
//
// 'file_size' is the true total size of the file being probed; 'size' is
// how much of it is actually available at 'data' (a FILETYPE probe may only
// hand over a short prefix). Pass file_size == size when the full file is
// already loaded (decode path): a table that then runs past 'size' is a
// genuine format mismatch, not a truncated view, and must not be excused.
int IsOpoonaMOL (const u8 *data, size_t size, size_t file_size);

// Lists every named record (index, name, offset, size); "NULL" placeholder
// slots are reported as such, not skipped.
enumError DecodeOpoonaMOL_Text (FILE *f, const u8 *data, size_t size);

//-----------------------------------------------------------------------------
// .mot animation clip: no magic bytes either. Structural probe validates
// the header shape and the bone bind-pose array (see lib-opoona.c).
int IsOpoonaMOT (const u8 *data, size_t size, size_t file_size);

// Dumps the header fields and the full bone bind-pose array (scale,
// translate, quaternion) for every bone. If animation-curve data follows
// the bone array (i.e. the file is longer than the bind-pose array alone),
// that byte range is reported by offset and length only -- it is never
// decoded or guessed at, since its layout is not understood.
enumError DecodeOpoonaMOT_Text (FILE *f, const u8 *data, size_t size);

#endif // LIB_OPOONA_H
