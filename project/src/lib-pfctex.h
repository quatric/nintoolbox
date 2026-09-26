#ifndef LIB_PFCTEX_H
#define LIB_PFCTEX_H

#include "lib-nintendo.h"

// Nintendo Pocket Football Club (CTR-P-AHBP, prototype v0.1.0 romfs dump)
// UI texture container. Internally these assets belong to the "CALCIO3DS"
// engine (see the magic string in data/demodata6.bin); no public name is
// documented for this specific container, so it's named after the game.
//
// Layout (20-byte header, no text magic):
//   u32 one          -- always 1
//   u32 header_size  -- always 0x14 (20)
//   u32 legacy_size  -- unused for decoding; meaning not recovered
//   u16 frame_count  -- unused for decoding (does not gate pixel layout)
//   u16 tile_w       -- image width / 8
//   u16 tile_h       -- image height / 8
//   u16 unknown
// (yes, frame_count sits before tile_w/tile_h, not after -- verified against
// the actual byte offsets, not just field order intuition)
// offset 0x14: a standard Nintendo LZ11 stream (magic byte 0x11) running to
// EOF. Decompressing it yields the base image as an 8x8-tile Morton-order
// (BFLIM tile_mode 1), RGBA4444 surface, top-left origin, width=tile_w*8,
// height=tile_h*8, no flip. Any bytes beyond width*height*2 are additional
// mip levels, ignored here.
typedef struct pfctex_info_t
{
	uint width;
	uint height;
} pfctex_info_t;

// Structural probe (no text magic exists for this format): validates the
// fixed header words, the LZ11 marker at offset 0x14, and that the derived
// base-image size fits inside the LZ11-declared decompressed length.
bool IsPFCTex (const u8 *data, uint size);

enumError ScanPFCTex (pfctex_info_t *info, const u8 *data, uint size);

// Decodes the base (mip 0) image to RGBA8. *dest is MALLOC'd.
enumError DecodePFCTex_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, uint size);

#endif
