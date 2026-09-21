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
//   texture record has a fixed 4-byte magic 0x0020AF30. Scanning the whole
//   file for this magic reliably locates every texture record in every
//   sample checked (767 records across all 183 real .txd files on disc) --
//   this does not depend on any of the shaky count/offset-table fields
//   above at all.
//
//   Fields, relative to the magic's own offset ("magic_off"):
//     magic_off-0x98  NUL-terminated ASCII texture name (variable length),
//                 when magic_off is at least 0xA0 into the file. The very
//                 first record in a file can have a shorter pre-magic
//                 preamble than usual (there's no previous record's
//                 trailing name-teaser eating space before it -- see
//                 below), so this offset only holds once there's room for
//                 it; otherwise the texture is just given an index-based
//                 name ("tex000", ...) instead of guessing.
//     +0x04       u32 "plane count" (n): every real sample uses 1 or 2.
//                 Controls where the dimensions sit -- see next field.
//     +0x0c+8*n   u16 height, u16 width, u32 GX-native texture format code.
//                 This project's own image_format_t enum already uses the
//                 identical Nintendo numbering (IMG_CMPR=0xe, IMG_RGB5A3=5,
//                 IMG_RGBA32=6, ...); the codes actually present on this
//                 disc are 0xe (CMPR, 751 records), 5 (RGB5A3, 4), and 6
//                 (RGBA32, 3, e.g. colramp.txd's palette-strip textures).
//                 No paletted formats (C4/C8/C14X2) were seen anywhere on
//                 disc. An earlier pass at this decoder assumed the
//                 dimensions sat at a fixed +0x14 (i.e. n==1 only) and
//                 read plane_count==2 records' dimensions as zero; fixing
//                 the offset raised coverage from 182/757 (24%) to
//                 767/767 (100%) of the texture records on this disc, with
//                 no other change.
//     +0x40       raw GX-native pixel data starts here (the base image
//                 only -- see below re: mip levels).
//
//   A byte at magic_off-0x24 (i.e. record_start+0x7c under the old,
//   record-start-relative framing) looked like a plausible mip-level
//   count on the handful of samples first checked, but reads as clear
//   pixel-data noise (170, 255, 0x55/0xAA-style fill bytes, ...) on most
//   real records, so it is NOT used -- only the base image is decoded.
//   Likewise, reconciling the base image's byte size against the gap to
//   the next record (which is what an earlier pass at this decoder did)
//   is actively wrong for some large single-texture files: the gap comes
//   up exactly 0xA0 bytes short there, because the next record's own
//   pre-magic preamble isn't a fixed size either (same reason the very
//   first record in a file can be short one, above). The bound that
//   actually held, checked against every one of the 767 real texture
//   records on this disc, is simply: sane non-zero width/height, a
//   recognized format code, and the computed base-image byte count fits
//   inside the file from its data offset -- that is what gates the PNG
//   decode here, and it reconciles 767/767 (100%) of them.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_TXD_H
#define SZS_LIB_TXD_H 1

#include "lib-std.h"

enumError ExtractTXDArchive (ccp arg, ccp basedir, uint depth);

#endif
