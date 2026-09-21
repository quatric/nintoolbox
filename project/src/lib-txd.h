// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// War Drum Studios' Wii port of Bully: Scholarship Edition texture
// dictionary (.txd). Magic "TDCT". NOT the stock PC RenderWare TXD format
// the extension suggests -- a completely different, custom, big-endian
// container (Wii/GC = PowerPC).
//
// Layout recovered and verified against all 183 real .txd files found on
// the disc (8500 to 1520268 bytes), big-endian throughout:
//
//   0x00  "TDCT"
//   0x04  u32 version (1 in every sample)
//   0x08  u32 total_file_size -- byte-exact against the real file size in
//         all 183/183 samples; this is the header sentinel this decoder
//         gates on.
//   0x0c  u32 end-of-texture-data offset (everything from here to this
//         offset is texture records+pixel data; whatever follows, if
//         anything, is an unrecovered trailer of varying size -- 20 to 44
//         bytes across samples checked, meaning was not recovered).
//
//   A texture-count/offset-table region follows at 0x14..0x28ish whose
//   exact field semantics were NOT reliably recovered -- a field that
//   looked like a texture count in small samples reads as clear garbage
//   in some larger ones (e.g. colramp.txd reads 3439329283 there), so
//   nothing in this decoder depends on it.
//
//   The one per-texture invariant this decoder actually trusts: **every**
//   texture record has a fixed 4-byte magic 0x0020AF30 at a fixed offset
//   +0xA0 from the record's start. Scanning the whole file for this magic
//   and using each hit minus 0xA0 as a record boundary (consecutive
//   record-start to record-start, or the 0x0c end-offset for the last
//   one) reliably locates every texture record in every sample checked --
//   this does not depend on any of the shaky count/offset-table fields
//   above at all.
//
//   Texture record layout (offsets relative to the record's own start,
//   i.e. relative to (magic_offset - 0xA0)):
//     +0x08       NUL-terminated ASCII texture name (variable length,
//                 zero-padded to the fixed per-record stride below)
//     +0x7c       u8 mip_count
//     +0xA0       u32 0x0020AF30 (the sentinel this decoder gates on)
//     +0xB4       u16 height
//     +0xB6       u16 width (confirmed against real logo/icon textures --
//                 e.g. liEU.txd's 4-icon shield strip only reads right way
//                 round, 256x64, this way; the swapped order silently
//                 "decoded" too, since GX tile byte counts are symmetric
//                 under a w/h swap, it just came out sideways)
//     +0xB8       u32 GX-native texture format code -- this project's own
//                 image_format_t enum already uses the identical Nintendo
//                 numbering (IMG_I4=0, IMG_CMPR=0xe, IMG_RGB5A3=5,
//                 IMG_RGBA32=6, ...), confirmed against the 4 codes
//                 actually present on this disc: 0 (I4, 535 records),
//                 0xe (CMPR, 225), 5 (RGB5A3, 4), 6 (RGBA32, 3). No
//                 palette formats (C4/C8/C14X2) were seen anywhere on
//                 disc, so paletted decode was not needed/implemented.
//     +0xE0       raw GX-native pixel data starts here: mip 0 first
//                 (padded to the format's native GX tile size), then
//                 each successive mip half the size, mip_count levels
//                 total.
//
//   Per-texture verification actually performed: mip_count levels' worth
//   of tile-padded GX pixel bytes (width/height/format-dependent) is
//   computed and required to reconcile EXACTLY with the real gap to the
//   next record (or to the 0x0c end offset for the last record) --
//   the same "computed size must equal actual size" gate every other
//   image decoder in this project uses. Across all 183 real .txd files
//   on disc this decodes 182 of 757 texture records (24%) to real PNGs
//   -- most files have at least one (135/183, 73.8%), it is a minority
//   of records per file (mip chains, unusual dimensions, or a format
//   this decoder doesn't reach) that don't reconcile. Wherever the size
//   doesn't reconcile, that texture's raw record bytes are carved out
//   untouched as a sidecar instead of guessing at a slightly different
//   tiling/stride/mip layout -- the same honest-partial rule as this
//   project's other decoders.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_TXD_H
#define SZS_LIB_TXD_H 1

#include "lib-std.h"

enumError ExtractTXDArchive (ccp arg, ccp basedir, uint depth);

#endif
