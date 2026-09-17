#ifndef LIB_NUSHDB_H
#define LIB_NUSHDB_H

#include "lib-nintendo.h"
#include <stdio.h>

// Bandai Namco SSBH compiled-shader container (.nushdb), Super Smash Bros.
// Ultimate. Reference: ultimate-research/ssbh_lib ssbh_lib/src/formats/shdr.rs
// (Shdr::V12, Shader, ShaderStage).

bool IsNUSHDB (const u8 *data, size_t size);

// Lists every compiled shader entry (name, stage, and the raw GPU-binary blob's
// offset+size inside the file). The blob itself is an opaque NVN shader binary
// -- proprietary machine code with no public spec -- so, same scope as
// DecodeBNSH_Text, it is not disassembled here.
enumError DecodeNUSHDB_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_NUSHDB_H
