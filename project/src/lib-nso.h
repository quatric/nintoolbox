// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_NSO_H
#define LIB_NSO_H

#include "lib-nintendo.h"

// Nintendo Switch Executable (.nso / NSO0)
// Official format defined in NintendoSDK (Siglo) MakeNso / Nso.cs.

#define NSO0_MAGIC 0x304F534E // "NSO0"

typedef struct nso_segment_t
{
	u32 file_offset;
	u32 memory_offset;
	u32 decompressed_size;
	u32 align_or_bss;
} nso_segment_t;

typedef struct nso_header_t
{
	u32 magic; // "NSO0"
	u32 version;
	u32 reserved;
	u32 flags;
	nso_segment_t text;
	nso_segment_t ro;
	nso_segment_t data;
	u8 module_id[32];
	u32 text_comp_size;
	u32 ro_comp_size;
	u32 data_comp_size;
	u8 text_sha256[32];
	u8 ro_sha256[32];
	u8 data_sha256[32];
} nso_header_t;

bool IsNSO (const u8 *data, size_t size);
enumError ParseNSOHeader (nso_header_t *hdr, const u8 *data, size_t size);
enumError ExtractNSOArchive (ccp arg, ccp basedir, uint depth);
enumError DumpNSO (FILE *out, const u8 *data, size_t size);
enumError SaveTextNSO (ccp filename, const u8 *data, size_t size);

#endif // LIB_NSO_H
