#ifndef SZS_LIB_PRC_H
#define SZS_LIB_PRC_H 1

#include "types.h"
#include "file-type.h"

// Smash parameter binary (.prc / .param): Ultimate "paracobn" files are
// structurally decoded to ParamXML/prc-rs-dialect XML (bool/sbyte/byte/
// short/ushort/int/uint/float/hash40/string/list/struct, struct keys as
// hash="0x.........." and list items as index="N"); older Smash 4 era
// variants ("parambinary", "PRC\0", "BPAR") are recognised but have no
// public structural reference, so they decode to an honest placeholder.

// Returns true if 'data' starts with a recognised PRC header.
bool IsPRC (const u8 *data, size_t size);

// Decodes a PRC binary file into XML text.
enumError DecodePRC_XML (char **dest_xml, const u8 *data, size_t size);

#endif // SZS_LIB_PRC_H
