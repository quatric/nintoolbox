// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_NXFONT_H
#define SZS_LIB_NXFONT_H 1

#include "lib-std.h"

// NintendoWare complex font description (".bfcpx", magic "FCPX", BOM FFFE, header size 0x14, version, file size),
// which tells the layout runtime how to combine font files. After the header: u32 count, then offsets
// (relative to 0x14) of the root font nodes. A node starts with its kind:
//   0  bitmap font:   +4 name offset (relative to the node), e.g. "PicFont_00.Nin_Ounce_NVN2.bffnt"
//   1  scalable font: +4 f32 size, +0x14 alternate character, +0x24/+0x34 f32 scales,
//                     +0x2C name offset, +0x40 code range count, +0x44 range offset ({u32 first, u32 last})
//   2  pair font:     +4/+8 offsets of the two child nodes (primary, fallback)
// Mario Kart World pairs its race fonts with region fonts (.bfotf/.bfttf) this way.

bool IsFCPX (const u8 *data, size_t size);

// YAML description (MALLOC'd, free with FREE).
enumError ConvertFCPXToYaml (const u8 *data, size_t size, char **text, size_t *text_size);

// Switch and Switch 2 bitmap fonts (".bffnt", "FFNT" little endian, version 4): the glyph sheets of the TGLP
// section are stored GPU tiled (block linear; Tegra X1 or Orin GOBs, picked by which one gives the smoother
// image since the file names no platform). Formats by TGLP code: 0/14 RGBA8, 5 LA8, 8 A8, 9/15 BC1,
// 10/16 BC2, 11/17 BC3, 12 BC4, 13 BC5, 18/19 BC7.
bool IsNXFont (const u8 *data, size_t size);

// Decode sheet INDEX to a MALLOC'd RGBA8 image; *n_sheets gets the sheet count.
enumError DecodeNXFontSheet (const u8 *data, size_t size, uint index, u8 **rgba, uint *width, uint *height, uint *n_sheets);

#endif // SZS_LIB_NXFONT_H
