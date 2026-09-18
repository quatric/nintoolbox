#ifndef LIB_ADJB_H
#define LIB_ADJB_H

#include "lib-nintendo.h"
#include <stdio.h>

// Smash mesh triangle adjacency (model.adjb), Super Smash Bros. Ultimate.
// Reference: Ploaj/SSBHLib CrossMod/CrossMod/Nodes/Formats/Models/Adjb.cs
// (loaded beside its .numshb as "model.adjb"). No magic; little-endian:
//
//   0x00  s32 mesh count
//   then, per mesh: s32 id, s32 data offset (relative to the end of this
//   table, i.e. absolute 4 + 8*count + offset)
//   then, per mesh: u16 index buffer, sized by consecutive offsets
//   (the last mesh runs to end of file).
//
// This decoder reports each mesh's id and index list as text.

bool IsADJB (const u8 *data, size_t size);
enumError DecodeADJB_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_ADJB_H
