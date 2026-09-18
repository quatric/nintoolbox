#ifndef LIB_NUFXLB_H
#define LIB_NUFXLB_H

#include "lib-nintendo.h"
#include <stdio.h>

// Bandai Namco SSBH shader-effects library (.nufxlb), Super Smash Bros.
// Ultimate. Reference: ultimate-research/ssbh_lib ssbh_lib/src/formats/nufx.rs
// (Nufx::V0/V1, ShaderProgramV0/V1, ShaderStages, VertexAttribute,
// MaterialParameter).

bool IsNUFXLB (const u8 *data, size_t size);

// Lists every shader program: its name, render pass, the non-empty shader
// stage labels (these name entries in a sibling .nushdb, resolved by name
// there rather than here), and the vertex attributes (version 1.1 only) and
// material parameters it requires.
enumError DecodeNUFXLB_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_NUFXLB_H
