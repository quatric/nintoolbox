// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_ZELDABAKE_H
#define SZS_LIB_ZELDABAKE_H 1

#include "lib-std.h"

// Small data tables of the Switch 2 Zelda (Tears-of-the-Kingdom engine) romfs, dumped as YAML.
//
// Baked light list (".bklight", Bake/Light/<scene>.bklight): "BKLT", u32 version 1, u32 counts of
// point / spot / area lights, then the three arrays (little endian f32 unless noted):
//   point (28 bytes): position[3], color[3] (radiometric, thousands), scale
//   spot  (52):       position[3], direction[3], color[3], falloff[2], intensity, scale
//   area  (60):       position[3], direction[3], up[3] (orthogonal to direction), color[3], size[2], intensity
//
// Culling boxes (".bbox", PreComputedCulling/<scene>/Boxes.bbox): u32 version 2, u32 count, count x 44
// bytes {u32 id, u32 flags, center[3], reserved[3] (0), size[3]}; the low mantissa bits of the center
// floats carry noise, positions snap to 0.5.
//
// Foliage tables (".flnt"):
//   impostor info (Foliage/ImpostorInfo): u32 version 2, u32 0, u32 N names, u32 R records, N x 256 byte
//   pascal strings (u8 length, bytes), N x {u32 first record, u32 count, u32 kind, u32 0} ranges, R x 80
//   byte records (20 f32: impostor frame matrices).
//   node tree (Foliage/NodeTree*/<name>.foliage.flnt, 1168 bytes): {u8 nodes (<= 32), u8 records (<= 64), u16 0x100, f32, f32},
//   nodes x 12 bytes at 0x0c {u32 flags, u8[8] (7f 7f 7f 80 and 4 varying bytes)}, records x 12 bytes at 0x190
//   {u8 index, 0xff, u16 0, u8[8]}.

// Mesh codec packages (".bfres.mc", "MCPK"): only the container layout is decoded, not the payload.
//   0x00 "MCPK", u8 micro, u8 minor, u16 major (this game: 1.0.0), u32 flags,
//   0x0c u32 size A, u32 X, u32 size B, u32 0x803, then section A (starts with "FMSH": u32 version 0x10000, workMemSize,
//   two output sizes (X is about half of their sum), u32 0x129, then entropy coded data) and section B; 28 + A + B == file size.
// The payloads are not zstd frames and not the 0.x layout of the public MeshCodec decoder, which rejects major version 1.

bool IsBklt (const u8 *data, size_t size);
bool IsMcpk (const u8 *data, size_t size);
bool IsCullBoxes (const u8 *data, size_t size);
bool IsFoliageTable (const u8 *data, size_t size);

enumError ConvertMcpkToYaml (const u8 *data, size_t size, char **text, size_t *text_size);
enumError ConvertBkltToYaml (const u8 *data, size_t size, char **text, size_t *text_size);
enumError ConvertCullBoxesToYaml (const u8 *data, size_t size, char **text, size_t *text_size);
enumError ConvertFoliageTableToYaml (const u8 *data, size_t size, char **text, size_t *text_size);

#endif // SZS_LIB_ZELDABAKE_H
