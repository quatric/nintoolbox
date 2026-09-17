#ifndef LIB_WTB_H
#define LIB_WTB_H

#include "lib-nintendo.h"
#include <stdio.h>

// Nintendo Switch Texture Archive (.wta header + .wtp image data), used by some Switch ports
// (e.g. Fire Emblem Warriors). Reference: KillzXGaming/Switch-Toolbox
// File_Format_Library/FileFormats/Texture/WTB.cs. It is a dictionary of fixed-size
// TextureInfo records (GX2-style format codes, Tegra block-linear layout) plus two parallel
// offset/size tables that either point back into this same buffer or into a separate .wtp
// file, exactly like BNTX/GTX -- so pixel decoding is handed off to lib-bntx.c's existing
// block-linear deswizzler and BC/ASTC block decoders instead of reimplementing them.

typedef struct wtb_texture_t
{
	uint width, height, depth;
	uint format; // raw GX2-style format code (WTB TextureInfo.Format)
	uint mip_count;
	uint type; // WTB SurfaceType
	uint texture_layout; // low 3 bits are the block-height-log2, as in BNTX
	uint data_offset, data_size; // offset/size into the image-data blob (see wtb_t.external_data)
} wtb_texture_t;

typedef struct wtb_t
{
	const u8 *data; // the .wta header buffer that was scanned
	uint size;
	uint n_textures;
	wtb_texture_t *textures; // owned
	bool external_data; // true: image data lives in a sibling .wtp file, not in 'data'
} wtb_t;

bool IsWTB (const u8 *data, size_t size);
enumError ScanWTB (wtb_t *wtb, const u8 *data, uint size);
void ResetWTB (wtb_t *wtb);

// Text manifest of every texture's dimensions/format/mip count and the offset/size of its
// image data, plus whether that data lives in this same buffer or an external .wtp.
enumError DecodeWTB_Text (FILE *out, const wtb_t *wtb);

// Decodes texture INDEX to tightly packed RGBA8, by translating its GX2-style format code to
// the equivalent BNTX format and reusing DecodeBNTX_RGBA. IMAGE_DATA/IMAGE_DATA_SIZE is the
// buffer that actually holds the swizzled pixels: wtb->data itself when !external_data, or the
// loaded contents of the sibling .wtp file when external_data is set.
enumError DecodeWTB_RGBA (u8 **dest, uint *width, uint *height, const wtb_t *wtb,
	const u8 *image_data, uint image_data_size, uint index);

#endif // LIB_WTB_H
