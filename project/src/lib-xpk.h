// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Exient "XPK" archives (*.pak; Angry Birds Star Wars, Wii). No public
// documentation exists; the layout below was worked out from the four retail
// packages on that disc (scripts, levels, extras, images: 29 to 411 entries)
// and every entry verifies: each zlib member inflates to exactly its recorded
// size.
//
// All fields big-endian.
//   0x00 u32 magic       0x58504b01 ("XPK", 1)
//   0x04 u32 version     2, 3, 4 and 9 seen; the layout below is the same
//   0x08 u32 files       number of *file* entries (directory records extra)
//   0x0c u32 names_size  byte length of the NUL-separated name table
//   0x10..0x4f           counters and zeros, not needed
//   0x50 entry[]         32 bytes each, directory records first:
//          u32 0         always 0
//          u32 name      offset into the name table
//          u32 size      uncompressed size; 0 marks a directory
//          u32 offset    file: absolute data offset; directory: index of its
//                        first child entry
//          u32 flags     1 = zlib stream, 0 = stored (or a directory)
//          u32 mtime     Unix time
//          u32 csize     file: stored/compressed byte length (0 for stored
//                        files, whose length is `size`); directory: number of
//                        children
//          u32 0
//   then the name table, then the data. The entry count is not in the
//   header: the first file's data starts exactly where the name table ends,
//   so entries = (min file offset - names_size - 0x50) / 32.
//
// A directory owns the contiguous entry range [first child, first child +
// count); entries outside every range sit at the root. Ranges may contain
// further directories.
//
// Extract-only.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_XPK_H
#define SZS_LIB_XPK_H 1

#include "lib-nintendo.h"

bool IsXpk (const u8 *data, size_t size);
enumError ScanXpk (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif
