#ifndef LIB_RSTB_H
#define LIB_RSTB_H

#include "lib-nintendo.h"
#include <stdio.h>

// BOTW/TOTK Resource Size Table (.rstb / RSTB) -- flat crc32+name size lookup, no relocations.

// Returns true if 'data' starts with the "RSTB" magic.
bool IsRSTB (const u8 *data, size_t size);

// Decodes an RSTB file into a human-readable text listing.
enumError DecodeRSTB_Text (FILE *out, const u8 *data, size_t size);

// Switch 2 / Tears of the Kingdom resource size table ("RESTBL", ".rsizetable"): "RESTBL", u32 version,
// u32 name field size, u32 crc_count, u32 name_count, then { u32 crc32; u32 size; } crc_count times
// (sorted by crc) and { char name[name field size]; u32 size; } name_count times. The crc32 is taken of the
// romfs path without a leading slash and without ".zs".
bool IsRESTBL (const u8 *data, size_t size);

// Decodes a RESTBL file into the same text layout as DecodeRSTB_Text(). With 'romfs_root' set, every
// file below that directory is hashed and matching entries get their path as a comment.
enumError DecodeRESTBL_Text (FILE *out, const u8 *data, size_t size, const char *romfs_root);

// Encodes a previously decoded RSTB text listing back into binary form.
enumError EncodeRSTB_Text (u8 **dest, uint *dest_size, const char *text, uint text_len, bool is_le);

#endif // LIB_RSTB_H
