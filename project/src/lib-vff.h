// Nintendo VFF ("VFF ") -- a PrFILE2 (eSOL) Virtual FAT volume, as used by Wii
// channels and save data to hold a small FAT12/FAT16 filesystem inside a
// single file.
#ifndef SZS_LIB_VFF_H
#define SZS_LIB_VFF_H 1

#include "types.h"

// True when DATA begins with a VFF header whose fields agree with its size.
bool IsVFF (const u8 *data, uint size);

// Extract every file in the volume, directories and all.
enumError ExtractVFFArchive (ccp arg, ccp basedir, uint depth);

#endif // SZS_LIB_VFF_H
