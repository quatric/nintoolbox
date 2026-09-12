// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_G1T_H
#define LIB_G1T_H 1

#include "lib-nintendo.h"

// Koei Tecmo G1T texture container
enumError ExtractG1TArchive (ccp arg, ccp basedir, uint depth);

// Wii U chunked wrapper (.g1t.gz): u32 magic 0x10000 + u32 stream
// count + u32 decompressed size + u32[count] table, then size-prefixed
// zlib streams (table[i] = stream bytes + 4) with zero padding between
// them; a trailing table entry with no stream left appends that many zero
// bytes instead. Output is the bare container (G1TG on Wii U retail).
enumError DecodeG1TGZ (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

// Header + first-stream probe (no decompression).
bool IsG1TGZ (const u8 *data, uint size);

#endif // LIB_G1T_H
