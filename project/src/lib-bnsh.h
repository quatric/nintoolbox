#ifndef LIB_BNSH_H
#define LIB_BNSH_H

#include "lib-nintendo.h"
#include <stdio.h>

// Nintendo Switch Binary Shader (.bnsh). Reference: KillzXGaming/Switch-Toolbox
// File_Format_Library/FileFormats/Shader/BNSH.cs Header/ShaderVariation/ShaderProgram/ShaderData.

bool IsBNSH (const u8 *data, size_t size);

// Lists the shader variations found in a BNSH container and, for each present stage
// (vertex/tess-control/tess-eval/geometry/fragment/compute), the raw blob offset(s) and
// size(s) inside the file. The GPU machine code / GLSL-NX source itself is version- and
// target-dependent and not disassembled here -- this only exposes enough to let the caller
// pull the raw bytes back out, the same scope as the AAMP header-only decode.
enumError DecodeBNSH_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_BNSH_H
