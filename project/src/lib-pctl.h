#ifndef LIB_PCTL_H
#define LIB_PCTL_H

#include "lib-nintendo.h"
#include <stdio.h>

// NintendoWare particle-effect archive (.ptcl / .eset, magic "VFXB"). Reference: KillzXGaming/
// Switch-Toolbox File_Format_Library/FileFormats/Effects/PCTL.cs and EffectLibrary.

bool IsPCTL (const u8 *data, size_t size);

// Decodes VFXB header, section manifest, emitters, textures, models, and shaders to text.
enumError DecodePCTL_Text (FILE *out, const u8 *data, size_t size);

// Extracts a VFXB / .ptcl particle archive into dest_dir:
// - PtclHeader.txt: Header parameters and effect archive name
// - Base.ptcl: Exact duplicate copy of the particle file
// - textures.bntx: Embedded Switch BNTX texture archive (from GRTF)
// - models.bfres: Embedded Switch BFRES 3D model archive (from G3PR)
// - shaders.bnsh: Embedded Switch BNSH shader archive (from GRSN)
// - Emitter hierarchy: EmitterSetInfo.txt and emitter directories with EmitterData.bin
// - primitives.bin: Primitive geometry buffer (from PRMA)
// - textures_gx2: Wii U GX2 texture data (from TEXA/EFTB)
enumError ExtractPCTLArchive (ccp source_file, ccp dest_dir);

// Reconstructs a VFXB / .ptcl particle archive from source_dir into dest_file.
enumError CreatePCTLArchive (ccp source_dir, ccp dest_file);

#endif // LIB_PCTL_H
