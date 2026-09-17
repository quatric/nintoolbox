#ifndef LIB_RSTB_H
#define LIB_RSTB_H

#include "lib-nintendo.h"
#include <stdio.h>

// BOTW/TOTK Resource Size Table (.rstb / RSTB) -- flat crc32+name size lookup, no relocations.

// Returns true if 'data' starts with the "RSTB" magic.
bool IsRSTB (const u8 *data, size_t size);

// Decodes an RSTB file into a human-readable text listing.
enumError DecodeRSTB_Text (FILE *out, const u8 *data, size_t size);

// Encodes a previously decoded RSTB text listing back into binary form.
enumError EncodeRSTB_Text (u8 **dest, uint *dest_size, const char *text, uint text_len, bool is_le);

#endif // LIB_RSTB_H
