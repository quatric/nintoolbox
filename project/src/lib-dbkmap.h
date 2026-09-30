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
// Extract-only.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_DBKMAP_H
#define SZS_LIB_DBKMAP_H 1

#include "lib-nintendo.h"

bool IsDbkMap (const u8 *data, size_t size);
enumError ScanDbkMap (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif
