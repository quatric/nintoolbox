// SPDX-License-Identifier: GPL-2.0+
// Casper's Scare School: Spooky Sports Day (Wii, A2M) texture format (.tex).
//
// No public documentation of this title's formats exists. Reverse-engineered
// from the retail USA disc's own DATA/files tree by cross-checking ~460
// sample .tex files.
//
// What is understood and implemented here:
//   - .tex header: a fixed 24-byte big-endian header (magic, GX-style pixel
//     format code, encoded payload size, width, height, a constant trailing
//     field seen as 1 in every sample) followed directly by pixel data.
//     Confirmed across the sample set: 'width' and 'height' always equal
//     the ASCII-adjacent dimensions used elsewhere in the disc's assets,
//     and 'payload_size' is consistent with GameCube/Wii GX texture-cache
//     encodings (e.g. format 11 payloads are always exactly 2*w*h, matching
//     an uncompressed RGBA-class format; formats 9/10 are visibly smaller,
//     consistent with paletted/compressed GX formats).
//
// What is explicitly NOT understood or decoded: the exact GX pixel-format
// enum this game uses (its numbering does not match Nintendo's standard
// GX_TF_* values directly -- values 9, 10 and 11 were observed, and only
// their relative sizes were cross-checked, not their tiling/swizzle), and
// the meaning of the constant trailing header field (always 1 in every
// sample seen; possibly a mip-map count). Pixel data is therefore reported
// as a raw, un-decoded byte range rather than guessed at.
//
// Related sibling formats seen in the same disc tree, NOT decoded here:
//   - .anm (skeletal animation) and .msh (mesh) both open with the same
//     2-byte 0x04 0x02 tag as .tex, followed by an embedded ASCII object/
//     bone name (e.g. "Object01") or an ASCII section tag (e.g.
//     "Textures"). They are evidently siblings in the same A2M asset
//     container family, but their record layouts were not pinned down in
//     the time available and are left for future work.
//   - .fnt is a standard, publicly documented AngelCode BMFont XML file
//     (plain text, starts with "<?xml ... <font>"); no custom parsing
//     needed.
//   - .jbf ("pspbrwse.jbf") is a standard Paint Shop Pro / JASC thumbnail
//     browser cache file (magic "JASC BROWS FILE"), a leftover development
//     artifact rather than a runtime game asset; not a game format.
#ifndef LIB_CASPER_H
#define LIB_CASPER_H 1

#include "lib-std.h"

//-----------------------------------------------------------------------------
// .tex texture header, all fields big-endian:
//   u8  magic[2];      // always 04 02
//   u16 unused0;        // always 0000 in every sample seen
//   u32 pixel_format;   // GX-style format code; 9, 10 and 11 observed
//   u32 payload_size;   // size in bytes of the pixel data that follows
//   u32 width;          // texture width in pixels
//   u32 height;         // texture height in pixels
//   u32 flag1;           // always 1 in every sample seen (mip count?)
// followed immediately by 'payload_size' bytes of raw (un-decoded) pixel
// data.
#define CASPER_TEX_HEADER_SIZE 24
#define CASPER_TEX_MAGIC0 0x04
#define CASPER_TEX_MAGIC1 0x02

// Structural probe: validates the magic, the zero padding, a plausible
// pixel_format (9..11 observed; a small tolerance above that is allowed
// for formats not seen in the sample set), and that width/height/
// payload_size are internally consistent and fit within 'file_size'.
// 'size' is how much of the file is actually available at 'data' (a
// FILETYPE probe may only hand over a short prefix); 'file_size' is the
// true total size of the file being probed.
int IsCasperTEX (const u8 *data, size_t size, size_t file_size);

// Prints the decoded header fields as text; the pixel payload itself is
// reported only as an offset+size range, not decoded.
enumError DecodeCasperTEX_Text (FILE *f, const u8 *data, size_t size);

#endif // LIB_CASPER_H
