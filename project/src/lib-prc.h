#ifndef SZS_LIB_PRC_H
#define SZS_LIB_PRC_H 1

#include "types.h"
#include "file-type.h"

// Smash Parameter Binary (.prc / parambinary) container format used in Super Smash Bros. Ultimate and Smash 4.

// Returns true if 'data' starts with parambinary or PRC header.
bool IsPRC (const u8 *data, size_t size);

// Decodes a PRC binary file into XML text.
enumError DecodePRC_XML (char **dest_xml, const u8 *data, size_t size);

#endif // SZS_LIB_PRC_H
