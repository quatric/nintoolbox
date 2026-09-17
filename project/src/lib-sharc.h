#ifndef LIB_SHARC_H
#define LIB_SHARC_H

#include "lib-nintendo.h"
#include <stdio.h>

// NintendoWare Shader Source Archive (.sharc / AAHS) and its binary counterpart SHARCFB
// (.sharcfb / BAHS). Reference: KillzXGaming/Switch-Toolbox Shader/SHARC/SHARC.cs and SHARCFB.cs.

bool IsSHARC (const u8 *data, size_t size);
bool IsSHARCFB (const u8 *data, size_t size);

// Lists program/source-file names found in a SHARC archive. Each entry in the file is
// self-delimiting (it starts with its own section size), so we can walk and name every entry
// without needing to understand the shader-variation payload inside it.
enumError DecodeSHARC_Text (FILE *out, const u8 *data, size_t size);

// SHARCFB packs compiled GPU binaries rather than source text; the per-shader payload layout
// differs between the Wii U and Switch (NX) variant. Both are decoded: the Wii U layout down to
// its archive name, and the NX layout's full Header/Variations/ShaderPrograms/value-table
// structure (see SHARCFBNX.cs).
enumError DecodeSHARCFB_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_SHARC_H
