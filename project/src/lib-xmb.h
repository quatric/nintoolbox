#ifndef LIB_XMB_H
#define LIB_XMB_H

#include "lib-nintendo.h"
#include <stdio.h>

// Smash XMB material/LOD metadata (.xmb, "XMB "), Super Smash Bros. 4 /
// Ultimate. Reference: Sammi-Husky/SSBU-TOOLS XMBDec.py (MIT, the script
// Ploaj/SSBHLib's XMBLib/ was adapted from) and ultimate-research/xmb_lib
// (the maintained Rust successor, same XML mapping).
//
// Little-endian header (44 bytes, all u32):
//   0x00  "XMB " magic
//   0x04  node (entry) count
//   0x08  value (attribute) count -- informational, sum of per-node counts
//   0x0c  property (distinct name string) count -- informational
//   0x10  mapped-node (id lookup) count
//   0x14  string-offsets table offset (informational index, unused on decode)
//   0x18  node table offset (count x 16-byte entries)
//   0x1c  property table offset (value-count x 8-byte entries)
//   0x20  node-map offset (mapped-count x 8-byte id lookups)
//   0x24  names string-table offset
//   0x28  values string-table offset
//
// A node entry is 16 bytes: u32 name offset (into the names table),
// i16 property count, i16 child count, i16 first-property index,
// i16 unk1, i16 parent index (-1 for the root), i16 unk2.
// A property entry is 8 bytes: u32 name offset (names table) +
// u32 value offset (values table). A node-map entry is 8 bytes:
// u32 value offset (values table, the "id") + u32 node index.
// All strings are NUL-terminated. Decode emits the same XML mapping as
// XMBDec.py: one element per node, properties as XML attributes.

bool IsXMB (const u8 *data, size_t size);
enumError DecodeXMB_XML (FILE *out, const u8 *data, size_t size);

#endif // LIB_XMB_H
