#ifndef LIB_PCTL_H
#define LIB_PCTL_H

#include "lib-nintendo.h"
#include <stdio.h>

// NintendoWare particle-effect archive (.ptcl / .eset, magic "VFXB"). Reference: KillzXGaming/
// Switch-Toolbox File_Format_Library/FileFormats/Effects/PCTL.cs PTCL.Header/SectionBase.

bool IsPCTL (const u8 *data, size_t size);

// Walks the VFXB section tree (recursive, magic/size/offset per node, matching the "manifest"
// scope already used for AAMP/BNSH/BFSHA) and, for a handful of section types whose payload is a
// fixed, non-version-branching binary layout in the reference (TEXR texture info, EMTR/ESET/ESFT
// name strings, GTNT texture descriptor list), decodes that payload too. The emitter particle
// parameter block itself (colors/curves/samplers under EMTR) uses reference offsets that change
// per VFXVersion in Switch-Toolbox and isn't decoded here -- it's reported as a byte range only.
enumError DecodePCTL_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_PCTL_H
