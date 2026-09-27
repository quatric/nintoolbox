#ifndef LIB_PFS0_H
#define LIB_PFS0_H

#include "lib-nintendo.h"

// Nintendo Switch Partition File System (PFS0 / HFS0 / .nsp / .pfs0 / .hfs0)
// Official format defined in NintendoSDK (Siglo) PartitionFileSystemMeta.

#define PFS0_MAGIC 0x30534650 // "PFS0"
#define HFS0_MAGIC 0x30534648 // "HFS0"

bool IsPFS0 (const u8 *data, uint size);
bool IsHFS0 (const u8 *data, uint size);
enumError ExtractPFS0Archive (ccp arg, ccp basedir, uint depth);

#endif // LIB_PFS0_H
