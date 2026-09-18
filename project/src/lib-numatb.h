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

// Exports the same entries in Ploaj/SSBHLib MatLab's MaterialLibrary XML
// dialect (MatlXml/MatlSerialization.cs: <Material shaderLabel materialLabel>
// with <Parameter name> children holding <Float>/<Bool>/<Vector4>/<String>/
// <Sampler>/<UVtransform>/<BlendState>/<RasterizerState> values), so output
// can be compared with, and fed to, MatLab. Field shapes mirror MatLab
// exactly, including its version-agnostic 10-field BlendState and 8-field
// RasterizerState. Two deliberate byte-level differences from MatLab's own
// bytes: the declaration says utf-8 (this writer emits UTF-8, MatLab's
// StringWriter claims utf-16) and large/small floats use C %g spelling
// (1e+10) rather than C#'s (1E+10).
enumError DecodeNUMATB_XML (FILE *out, const u8 *data, size_t size);

#endif // LIB_NUMATB_H
