// SPDX-License-Identifier: GPL-2.0+
// "Diabolik: The Original Sin" (Wii, Europe En/Fr/De/Es/It/Nl disc)
// proprietary tagged-block resource container. No public documentation of
// this format exists. Reverse-engineered from scratch against the retail
// disc by extracting representative samples of every file kind that uses
// it and cross-checking the shared header/block layout byte-for-byte
// across all of them (.cfg, .gam, .loc under three different directories,
// .ls, .rgn).
//
// One container format is shared, unchanged, across every one of these
// extensions -- the extension only tells you which subsystem produced the
// file, not a different binary layout:
//   files/Configurations/*.cfg               - engine/platform config
//   files/Games/*.gam                        - per-title metadata
//   files/Games/<lang>/*.loc                 - localized dialogue text
//                                               (+ embedded per-line audio)
//   files/LoadingScreens/*.ls                - loading-screen definitions
//   files/LoadingScreens/<lang>/*.loc         - localized loading-screen text
//   files/Regions/*.rgn                      - room/region metadata
//   files/Regions/<lang>/*.loc                - localized region/subtitle text
//
// Confirmed file layout (all multi-byte fields big-endian, matching the
// Wii's native PowerPC byte order):
//   u32 magic       = 0xFAAFFAAF   (fixed, every sample seen)
//   u32 header_len  = 8            (fixed, every sample seen)
//   ... a tree of TAG/SIZE blocks, see below, filling the rest of the file ...
//   u32 eof_tag     = 0xFEEFFEEF   (fixed, last 4 bytes of every sample seen)
//
// A block is:
//   u32 tag;             // 0xBBBBBBBB "SECTION" or 0xBEBEBEBE "LEAF"
//   u32 size;             // counts this tag+size header PLUS the payload,
//                          // so payload length is (size - 8) and the next
//                          // sibling block starts at (block_offset + size)
//   u8  payload[size-8];
// The very first block (immediately after the 8-byte file header) always
// spans the rest of the file: its size field equals (file_size - 8).
//
// SECTION blocks are structural nodes: their payload typically opens with
// one or more plain u32 fields (child count / record kind / flags) before
// nested child blocks, so children are not always adjacent to the start of
// the payload -- the exact field layout differs per file kind (.cfg engine
// settings vs .gam metadata vs .rgn room table vs .ls screen list vs .loc
// text+audio) and was not fully pinned down field-by-field.
// LEAF blocks are values: small ones hold flags/counts; large ones (seen
// only inside language .loc files, several KB to several MB) hold a raw
// binary blob -- almost certainly compressed/ADPCM dialogue audio bundled
// per subtitle line, since the size lines up with per-language voice-over
// runtime and its offset always immediately follows a subtitle's UTF-16BE
// text block.
//
// Two length-prefixed string encodings are used for readable content and
// are fully decoded by this module wherever found:
//   - Names/ids (object names, region ids like "RG_03_09b"): u32 length +
//     that many ASCII bytes, NUL-padded to a round field size (20 bytes in
//     every sample seen, e.g. "Diabolik", "Default").
//   - Localized dialogue/subtitle text (seen only in .loc files): u32
//     char-count + that many UTF-16BE code units (BMP only; no surrogate
//     pairs observed in any sample), e.g. "DIABOLIK DRESSED AS A WAITER".
//
// This module does NOT claim a complete field-by-field schema -- the
// meaning of each plain numeric field differs per file kind and was not
// individually reverse-engineered. What IS decoded, robustly, for every
// file using this container: the full TAG/SIZE block tree (offset, size,
// nesting), every embedded ASCII and UTF-16BE string, and the location of
// every large LEAF blob (candidate embedded audio) -- printed by
// DecodeDiabolikRes_Text() below.

#ifndef SZS_LIB_DIABOLIK_H
#define SZS_LIB_DIABOLIK_H 1

#include "types.h"
#include <stdio.h>

//-----------------------------------------------------------------------------
// shared "FAAFFAAF" tagged-block resource container
// (.cfg / .gam / .loc / .ls / .rgn)

int IsDiabolikRes (const u8 *data, size_t size, size_t file_size);
enumError DecodeDiabolikRes_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // SZS_LIB_DIABOLIK_H
