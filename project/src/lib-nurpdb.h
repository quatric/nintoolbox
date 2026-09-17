#ifndef LIB_NURPDB_H
#define LIB_NURPDB_H

#include "lib-nintendo.h"
#include <stdio.h>

// Bandai Namco SSBH render-pass data (.nurpdb), Super Smash Bros. Ultimate.
// Reference: ultimate-research/ssbh_lib ssbh_lib/src/formats/nrpd.rs
// (Nrpd::V16). This is the least well-understood member of the SSBH family
// even in the reference itself -- most of its nested structs are named
// "Unk*"/"unk*" there and marked TODO, with several RelPtr64<u64> fields
// whose target isn't confidently known to be an array at all. Decoding here
// is scoped accordingly: framebuffers, state objects (samplers/rasterizer/
// depth/blend state), render passes, and the two auxiliary string lists are
// listed by name (and, where the reference is confident, their scalar
// fields); the render pass's own per-pass data items (RenderPassData's many
// variants) are not decoded, since the reference itself isn't sure of their
// layout.

bool IsNURPDB (const u8 *data, size_t size);
enumError DecodeNURPDB_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_NURPDB_H
