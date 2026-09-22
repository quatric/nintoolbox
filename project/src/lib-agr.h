// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Bully: Scholarship Edition (Wii) animation clip container (.agr, DATA/
// files/Anim). Big-endian, no compression, a flat sequence of self-sized
// "ANIM" chunks running to EOF:
//
//   per chunk:
//     +0x00  "ANIM"
//     +0x04  u32 (constant 1 in every sample checked; meaning not
//            recovered -- a format version, most likely)
//     +0x08  u32 chunk_size: self-inclusive from this field's own offset to
//            the end of the chunk, i.e. the chunk's total byte length is
//            8+chunk_size. Verified against all three real .agr samples on
//            disc (59, 418 and 439 chunks respectively): summing
//            8+chunk_size across every chunk lands exactly on the real
//            file size with no gap or overlap.
//     +0x0c  u32 (varies per chunk; likely a per-clip ID/hash or a
//            packed frame-count+flags field -- not recovered)
//     +0x10  the actual keyframe/track data: NOT decoded by this tool.
//            A handful of chunks contain what look like short printable
//            runs, but they don't recur across samples and don't look
//            like real clip names (unlike e.g. the TXD or CSI decoders'
//            embedded names), so no attempt is made to name clips.
//
// This only slices the container into per-clip raw files; actually
// decoding a clip's bone/keyframe data (which would need matching it up
// against the sibling .HXD skeleton-hierarchy files elsewhere in
// DATA/files/Anim) is a separate, larger reverse-engineering task not
// attempted here -- same honest-partial rule as this project's other
// container-only decoders (map.bad's geometry.bin, CSI's keyframe data).
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_AGR_H
#define SZS_LIB_AGR_H 1

#include "lib-std.h"

enumError ExtractAGRArchive (ccp arg, ccp basedir, uint depth);

#endif
