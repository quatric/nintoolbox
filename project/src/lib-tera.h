// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_TERA_H
#define SZS_LIB_TERA_H 1

#include "lib-std.h"

// Nintendo "tera" terrain data of Mario Kart World (and other Switch 2 titles), found under
// romfs/TerrainArc/<scene>/ next to <scene>.tscb.
//
// Every tile layer (".hght", ".mate", ".norm", "*.extm") is a TARB file: a 0x14 byte header
// ("TARB", version, header size, payload size, zstd size or 0) and a payload that starts with a
// 0x34 byte info block:
//   0x00 u16 BE grid side (texels, margins included)   0x02 u8 margin   0x03 u8 array slices (GPU block holds that many surfaces)
//   0x04 u8[4] per-channel type of the "data" block (6 = 8 bit, 9 = 16 bit, 0b = 32 bit float, 08 08 07 = R11G11B10F)
//   0x08 u8 flags (0x80: data block is delta packed)
//   0x14 u8[4] format of the GPU block (as 0x04, or 0x80 xx xx code: 0x30 BC1, 0x34 BC3, 0xc6 BC5, 0xc8 BC6H)
//   0x19 u8 mip count
//   0x1C/0x24/0x2C { s32 offset relative to the field, u32 size }: data, aux and GPU blocks.
// The data block holds plain or delta packed pixels; the GPU block holds the same image (and its
// mips) block linear with Switch 2 (Orin) GOBs and a block height of one GOB.
//
// The scene file ".tscb" (version 0x090C here) lists the layers (name, extension, size, format),
// the terrain materials and the quadtree of areas with the layers each one ships.

bool IsTarbFile (const u8 *data, size_t size);
bool IsTscbFile (const u8 *data, size_t size);

typedef struct TeraImage_t
{
	u8 *main; // PNG, or Radiance HDR when 'hdr' is set
	size_t main_size;
	u8 *extra; // NULL or a grayscale PNG of the 4th channel (when it carries data)
	size_t extra_size;
	bool hdr;
	uint width, height;
	char desc[80]; // e.g. "260x260 R16 (delta packed)"
} TeraImage_t;

void ResetTeraImage (TeraImage_t *img);

// Decode one TARB tile. 'normal_map' selects the 2 channel normal-map reconstruction (".norm").
enumError DecodeTarb (const u8 *data, size_t size, bool normal_map, TeraImage_t *img);

// Dump a .tscb scene as YAML (MALLOC'd text, free with FREE).
enumError ConvertTscbToYaml (const u8 *data, size_t size, char **text, size_t *text_size);

// Encode raw pixels as PNG in memory: CHANNELS 1 (gray), 2 (gray+alpha), 3 (RGB) or 4 (RGBA),
// DEPTH 8 or 16 (16 bit samples are given little endian). Result is MALLOC'd.
enumError EncodePngMem (const u8 *pixels, uint width, uint height, uint channels, uint depth,
	u8 **png, size_t *png_size);

#endif // SZS_LIB_TERA_H
