#ifndef LIB_NUP_H
#define LIB_NUP_H

#include "lib-nintendo.h"

// NUP ("NX Package", magic "NUP\x01"): an open container for decrypted Nintendo
// Switch / Switch 2 dumps. One file holds a base program, its updates and add-on
// content as plain ExeFS / RomFS trees; each file is stored raw or as one zstd frame.
// Layout: 0x80-byte superblock, a table of contents (packages, directories, files,
// child slots, string pool) hashed with SHA-256, then 16-byte aligned extents.

#define NUP_SUPERBLOCK_SIZE 0x80

bool IsNUP (const u8 *data, uint size);
enumError ExtractNUPArchive (ccp arg, ccp basedir, uint depth);

#endif // LIB_NUP_H
