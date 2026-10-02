#ifndef LIB_JUPITER_PCK_H
#define LIB_JUPITER_PCK_H

#include "file-type.h"
#include "dclib-types.h"

// Jupiter Corp / Square Enix Nintendo DS Model/Motion Package (*.pck;
// The World Ends with You / It's a Wonderful World / Kingdom Hearts: Re:coded; DS)
//
// Format layout:
// +0x00..+0x03: u32 header length in bytes (little-endian; always 8 + count * 4, padded to 16 bytes)
// +0x04..+0x07: u32 member count N (little-endian)
// +0x08..+0x08+N*4: N * u32 member byte sizes (little-endian)
// followed by 0-pad to header length, then contiguous uncompressed member payloads:
// BMD0 (model), BCA0 (skeletal animation), BTP0 (texture pattern animation),
// BTX0 (texture), BTA0 (material animation), BMA0 (visibility animation), COLI (collision data).

#define JUPITER_PCK_MAX_FILES 1024

bool IsJupiterPck (const u8 *data, uint data_size, u64 file_size);

#endif // LIB_JUPITER_PCK_H
