#ifndef LIB_NUMATB_H
#define LIB_NUMATB_H

#include "lib-nintendo.h"
#include <stdio.h>

// Bandai Namco SSBH material container (.numatb), Super Smash Bros. Ultimate.
// Reference: ultimate-research/ssbh_lib ssbh_lib/src/formats/matl.rs (Matl::V15/
// V16, MatlEntry, Attribute, ParamId, Param).

bool IsNUMATB (const u8 *data, size_t size);

// Lists every material entry (label, shader label) and its named parameters
// (ParamId's public name where known, otherwise the raw numeric ID) with a
// human-readable value for the common scalar/vector/string/sampler/blend-state/
// rasterizer-state parameter kinds.
enumError DecodeNUMATB_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_NUMATB_H
