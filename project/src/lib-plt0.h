#ifndef SZS_LIB_PLT0_H
#define SZS_LIB_PLT0_H 1

#include "lib-image.h"
#include "lib-nintendo.h"

enumError LoadPLT0 (Image_t *img, const u8 *data, uint data_size);

// Header-only variant of LoadPLT0(): resolves a PLT0 blob to its raw
// (still-encoded) palette format, entry count and a pointer to the raw
// 2-byte-per-entry palette array inside 'data' -- no image is decoded or
// allocated. For pairing a sibling TEX0's indexed pixels with the palette
// this fork's own PLT0 tool already knows how to read (see lib-plt0.c's
// PAL_IA8/RGB565/RGB5A3 mapping).
bool GetRawPLT0 (const u8 *data, uint data_size,
	palette_format_t *pform, // out: PAL_IA8/PAL_RGB565/PAL_RGB5A3
	uint *n_colors, // out: palette entry count
	const u8 **pal_ptr // out: pointer into 'data'
);

enumError EncodePLT0_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint num_colors, palette_format_t pform);

// Quantize RGBA image data to a palette of up to 'target_colors' entries in format 'pform',
// building the 16-bit raw palette and mapping each pixel to the closest palette color index
// using the 3-pass diversity & histogram quantization algorithm from yoshakami/plt0.
enumError QuantizePalette_PLT0 (const u8 *rgba, uint width, uint height, uint xwidth,
	palette_format_t pform, uint target_colors, u8 **out_raw_pal, uint *out_pal_size,
	uint *out_num_colors, u16 **out_indices);

#endif
