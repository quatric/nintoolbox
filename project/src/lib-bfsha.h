#ifndef LIB_BFSHA_H
#define LIB_BFSHA_H

#include "lib-nintendo.h"
#include <stdio.h>

// Nintendo Switch Binary Shader Archive (.bfsha). Reference: KillzXGaming/Switch-Toolbox
// File_Format_Library/FileFormats/Shader/BFSHA.cs, whose actual byte layout lives in the
// separate KillzXGaming/BfshaLibrary repo it wraps (ShaderLibrary/Switch/BfshaLoader.cs and
// ShaderLibrary/Structs.cs) -- BFSHA.cs itself only hands the stream to that library.

bool IsBFSHA (const u8 *data, size_t size);

// Lists the archive-level string table (name/path), every named shader model, and -- when the
// model's header version is one of the tractable fixed layouts (major 4/5/7/8/9) -- its shader
// program table (variation offset, used-attribute flags, per-stage sampler/uniform-block index
// counts) plus the raw static+dynamic option key row per program from the archive's key table.
// The BNSH-embedded GPU bytecode itself is out of scope, same as DecodeBNSH_Text.
enumError DecodeBFSHA_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_BFSHA_H
