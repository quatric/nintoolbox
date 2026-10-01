// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Artefacts Studio ".map" level databases (Diabolik: The Original Sin, Boot
// Camp Academy, Jillian Michaels Fitness Ultimatum, Dodge Racing, Build-A-Bear
// Workshop, ... on Wii: 30 of the audited retail discs). The file uses the
// same "FAAFFAAF" tagged-section stream as the .cfg/.gam/.loc/.ls/.rgn files
// described in lib-diabolik.h, but holds the serialized engine object graph of
// a level: hierarchy nodes, skeletons ("Bip01 ..."), models and textures.
//
// Stream layout, recovered from the game's own reader (main.dol) and checked
// against every .map of Boot Camp Academy: all fields big-endian.
//   u32 0xFAAFFAAF, u32 8         file header (0xFBBFFBBF = byte-swapped twin)
//   section:  u32 0xBBBBBBBB      begin tag (4-aligned)
//             u32 end             file offset of the matching closing tag
//             u32 version         per-class serialization version
//             payload             raw fields and nested sections
//             u32 0xBEBEBEBE      closing tag, located at `end`
//   u32 0xFEEFFEEF                end of file
// The whole file is one outermost section. Objects are polymorphic: each one
// is stored as a class id plus a section whose payload is that class'
// serialization routine's output, so the field meanings depend on the class.
//
// Textures (the only class fully decoded here) are an object section holding,
// in order: a name section (u32 length 20 + 20 ASCII bytes), an 8-byte record
// `fmt u8, 0 u8, 1 u8, width u16, height u16, 0 u8`, then a leaf section whose
// payload is `u8 1, u32 mip count, u32 byte size, u32 0` followed by the GX CMPR
// mip chain (plus 3 bytes of padding), tightly packed, largest level first (every one of the 304 textures
// in the 25 Boot Camp Academy maps has width * height / 2 bytes per level, at
// least one 8x8 tile per level).
//
// Meshes: a leaf section `u8 1` followed by the arrays of one model, read by
// the game in this order (all big-endian, arrays tightly packed):
//   u32 V,  V x 16 bytes   vertices: s16 x, y, z (4096 = 1.0, Z up), u8 bone0,
//                          u8 bone1, s8 nx, ny, nz (64 = 1.0), u8, 3 bytes
//                          and u8 of skin weight data
//   u32 T,  T x 4 bytes    texture coordinates: u16 u, v (1024 = 1.0)
//   u32 P,  P x 6 bytes    second position array (s16 x3, used by the
//                          shadow/outline pass)
//   u32 C,  C x 4 bytes    colour/tex array of that pass
//   32 times, in this order:
//     u8 n;  n x { u16 first-vertex, u16 vertex-count, u32 triangles,
//                  u16, u32 display-list-size, display list bytes,
//                  8 bytes (4 x u16 bone palette) }
//     u8 n;  n x { same without the trailing 8 bytes }
//     u8 n;  n x { u32 k, u32, k x 8 bytes }
// The display lists of the first kind are GX commands (0x98 triangle strips;
// 0x00 NOP) whose vertices are {u16 position, u16 normal, u16 texcoord}; the
// position/normal indices are relative to the entry's first vertex, the
// texcoord index is global. The 32 slots are material slots. The per-entry
// first-vertex/count pairs tile the vertex array exactly and the triangle
// counts match the display lists on the one mesh checked (Boot Camp Academy's
// drill sergeant: 1883 vertices, 3479 triangles). Vertex positions are in
// model space, so the bind pose needs no skeleton.
//
// Extract-only.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_DBKMAP_H
#define SZS_LIB_DBKMAP_H 1

#include "lib-nintendo.h"

bool IsDbkMap (const u8 *data, size_t size);
// Writes one bind-pose GLB per skinned mesh into DEST_DIR/models/. Returns the
// number written (0 if the file holds no mesh).
uint ExportDbkMapModels (const u8 *data, size_t size, const u8 *aux, size_t aux_size, ccp dest_dir);
// AUX is the shared game database (Games/*.gam) next to a map, or NULL: textures
// its materials use are added to the entries as "gam_" members.
enumError ScanDbkMap (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size,
	const u8 *aux, size_t aux_size);

#endif
