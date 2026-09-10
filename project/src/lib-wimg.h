#ifndef LIB_WIMG_H
#define LIB_WIMG_H

#include "lib-std.h"

// CyberConnect2 ".wimg" texture (And-Kensaku / "Kanji Sonomama" family, Wii).
//
// Big-endian header:
//   0x00  char[4]  "WIMG"
//   0x04  u32      header size / data offset (0x20)
//   0x08  u16 u16  width, height
//   0x0C  u8       pixel format: 1 = CI4, 2 = CI8, 9 = RGBA32
//   0x0D  u8 u8 u8 sub-flags (palette kind / mip count -- unused here)
//   0x10  u32      data offset (0x20)
//   0x14  u32      pixel-data byte size (0 for format 9)
//   0x18  u32 u32  reserved
//   0x20  ...      GX-tiled pixel data; CI4/CI8 are followed immediately by a
//                  straight RGBA8888 palette (16 or 256 entries).
//
// Decodes to a tightly packed width*height RGBA8 buffer; *rgba is
// dclib-allocated and ownership passes to the caller (free with FREE, or hand
// to SaveDecodedRGBAToPNG which takes ownership). Nothing is allocated on
// error.

enumError DecodeWIMG (u8 **rgba, uint *width, uint *height, const u8 *data, uint size);

#endif
