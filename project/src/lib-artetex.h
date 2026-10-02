#ifndef LIB_ARTE_TEX_H
#define LIB_ARTE_TEX_H

#include "file-type.h"
#include "dclib-types.h"

// ArtePiazza Nintendo DS Texture Container (.tex)
// Used in Dragon Quest IV: Chapters of the Chosen, Dragon Quest V: Hand of the
// Heavenly Bride, and Dragon Quest VI: Realms of Revelation.
//
// Magic variants:
//   - "TextureObject\0" (112-byte header, 0x70) - standard container
//   - "TextureData\0"   (56-byte header, 0x38)  - compact variant
//   - "Texture Data\0"  (56-byte header, 0x38)  - space-padded variant
//
// Formats:
//   - 1: A3I5 (3-bit alpha, 5-bit color index, 32-color palette)
//   - 3: 4-bpp indexed (16-color palette, 2 pixels per byte, low nibble first)
//   - 4: 8-bpp indexed (256-color palette, 1 pixel per byte)
//   - 6: A5I3 (5-bit alpha, 3-bit color index, 8-color palette)
//
// Palettes are stored as 16-bit little-endian RGB555 colors.

bool IsArteTexture (const u8 *data, uint data_size);

enumError DecodeArteTexture (
	u8 **out_rgba, uint *out_width, uint *out_height, const u8 *raw, uint raw_size);

#endif // LIB_ARTE_TEX_H
