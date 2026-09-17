#ifndef LIB_AAMP_H
#define LIB_AAMP_H

#include "lib-nintendo.h"
#include <stdio.h>

// Nintendo Parameter Archive (.aamp / AAMP), BOTW/TOTK-era Switch tagged key/value tree.
// Only the fixed v2 header is documented publicly with confidence (zeldamods.org); the node
// tree itself uses hashed, undocumented-length-implied offsets that vary with content, so this
// decoder validates and reports the header/section sizes rather than guessing at the tree walk.

// Returns true if 'data' starts with the "AAMP" magic.
bool IsAAMP (const u8 *data, size_t size);

// Decodes an AAMP file header into a human-readable text summary.
enumError DecodeAAMP_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_AAMP_H
