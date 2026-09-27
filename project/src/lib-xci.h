// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_XCI_H
#define LIB_XCI_H

#include "lib-nintendo.h"

// Nintendo Switch Game Cartridge Image (.xci / HEAD)
// Official format defined in NintendoSDK (Siglo) gc_Types.h / XciMeta.cpp.

#define XCI_HEAD_OFFSET 0x100
#define XCI_HEAD_MAGIC 0x44414548 // "HEAD"

typedef struct xci_header_t
{
	u32 magic;
	u32 rom_area_start_page;
	u32 backup_area_start_page;
	u8 key_index;
	u8 rom_size;
	u8 version;
	u8 flags;
	u64 package_id;
	u32 valid_data_end_page;
	u64 partition_fs_header_address;
	u64 partition_fs_header_size;
	u8 partition_fs_header_hash[32];
} xci_header_t;

bool IsXCI (const u8 *data, size_t size);
enumError ParseXCIHeader (xci_header_t *hdr, const u8 *data, size_t size);
enumError ExtractXCIArchive (ccp arg, ccp basedir, uint depth);
enumError DumpXCI (FILE *out, const u8 *data, size_t size);

#endif // LIB_XCI_H
