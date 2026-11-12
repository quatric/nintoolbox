// SPDX-License-Identifier: GPL-2.0+
// "Planet-G" Wii engine formats, named after the PLANETG_ root tags of its sprite files. The same family
// ships in Bermuda Triangle - Saving the Coral, Rock Blast, Veggy World, Ocean Commander and Arcade Shooting
// Gallery (Offshore Tycoon has the .pgf fonts), so the formats are named after their own content, not after
// one game. Every rule below was checked against every file of that extension on those six USA discs
// (.MWT 3,807 with the envelope, .MWG/.MSP 405, .PKI 24, .pgf 21).
//
// Plain TPL files also use the .MWT extension (386 on those discs); they are handled as TPL.
//
// (0) ".MWT" -- "GDATAVERSION" texture envelope, 0x40-byte header:
//       char magic[12];     // "GDATAVERSION"
//       u32  version;       // 2
//       u32  payload_size;  // size of the TPL that follows the header
//       u32  width, height;
//       u32  flags;         // 0 or 1
//       u8   zero[32];
//     then a standard TPL (magic 00 20 AF 30) and, with flags bit 0, a 1-bit hit mask of ceil(width / 8) *
//     height bytes: file_size == 0x40 + payload_size + (flags & 1 ? ceil(width / 8) * height : 0).
//
// (1) ".MWG" / ".MSP" -- "PLANETG" tagged-object serialization: u32 length + root type
//     ("PLANETG_SPRITECOLLECTION" version 2 on .MWG, "PLANETG_SPRITE" version 1 on .MSP), u32 version, then
//     length-prefixed ASCII tags interleaved with u32 fields. Backslash paths ending in .MWT are the sprite
//     textures. The tree shape and the meaning of the numeric fields are not decoded; the text output walks
//     the file heuristically (length-prefixed ASCII runs are tags, everything else a raw u32).
//
// (2) ".PKI" -- "IMAGE_WII_COMPACT_FILE_VERSION_1" pack of .MWT files:
//       char magic[32];     // not NUL-terminated
//       u32  entry_count;
//       entry_count x { u32 name_len; char name[name_len]; u32 size; u32 offset; }
//     Names are backslash paths ("DATA\...\01.MWT"); every entry lies inside the file, after the table.
//
// (3) ".pgf" -- bitmap font metrics:
//       u32  name_len; char name[name_len];  // family, e.g. "Tahoma", "Comic Sans MS"
//       u32  point_size;
//       u32  padding[4];                     // left, top, right, bottom
//       u32  glyph_x[256], glyph_y[256], glyph_w[256], glyph_h[256];
//       u32  atlas_width, atlas_height;      // not always powers of two (607 x 52, 1885 x 121, ...)
//     followed by run-length data: one token per run (u8 b != 0: b pixels; u8 0 + u32: a long run), sometimes
//     after one extra 0 byte; the runs cover exactly atlas_width x atlas_height pixels. Every run is one colour
//     of the font's texture (a separate .MWT, e.g. Tahoma16.pgf + TAHOMA16.MWT, checked pixel by pixel), but
//     the colour is not stored. Two-colour atlases (9 of the 21) alternate background / ink and decode to
//     PNG; anti-aliased or shadowed ones only mark colour changes. Glyph rectangles lie inside the atlas (one
//     real glyph overhangs by a pixel, so the check allows a small margin).

#ifndef SZS_LIB_BERMUDATRIANGLE_H
#define SZS_LIB_BERMUDATRIANGLE_H 1

#include "types.h"
#include <stdio.h>

//-----------------------------------------------------------------------------
// All detectors take the buffered bytes (SIZE) and the real file size (FILE_SIZE, >= SIZE).

// (0) ".MWT" "GDATAVERSION" texture envelope

int IsBermudaMwt (const u8 *data, size_t size, size_t file_size);
enumError DecodeBermudaMwt_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (1) ".MWG" / ".MSP" "PLANETG" tagged-object serialization

int IsBermudaPlanetG (const u8 *data, size_t size, size_t file_size);
enumError DecodeBermudaPlanetG_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (2) ".PKI" "IMAGE_WII_COMPACT_FILE_VERSION_1" texture-pack container

int IsBermudaPki (const u8 *data, size_t size, size_t file_size);
enumError DecodeBermudaPki_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// (3) ".pgf" font resource

int IsBermudaPgf (const u8 *data, size_t size, size_t file_size);
enumError DecodeBermudaPgf_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

// Needs the whole file. 0: no valid run tail, 1: two-colour atlas (decodable), 2: more colours (the runs only
// mark colour changes of the companion .MWT texture).
int GetBermudaPgfAtlasKind (const u8 *data, size_t size, size_t file_size);
// Two-colour atlas only: allocates WIDTH x HEIGHT RGBA8, white ink on transparent background.
enumError DecodeBermudaPgf_RGBA (u8 **rgba, uint *width, uint *height, const u8 *data, size_t size,
	size_t file_size);

#endif // SZS_LIB_BERMUDATRIANGLE_H
