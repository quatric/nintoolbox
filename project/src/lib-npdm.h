// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_NPDM_H
#define LIB_NPDM_H

#include "lib-nintendo.h"

// Nintendo Switch Program Descriptor (.npdm / META)
// Official format defined in NintendoSDK (Siglo) MakeMeta (Meta.cs / Aci.cs / Npdm.cs).

#define NPDM_META_MAGIC 0x4154454D // "META"
#define NPDM_ACI0_MAGIC 0x30494341 // "ACI0"
#define NPDM_ACID_MAGIC 0x44494341 // "ACID"

typedef struct npdm_meta_t
{
	u8 flags;
	u8 main_thread_priority;
	u8 main_thread_core;
	u32 system_resource_size;
	u32 version;
	u32 main_thread_stack_size;
	char name[17];
	char product_code[17];
	u32 aci_offset;
	u32 aci_size;
	u32 acid_offset;
	u32 acid_size;
	u64 program_id;
} npdm_meta_t;

bool IsNPDM (const u8 *data, size_t size);
enumError ParseNPDM (npdm_meta_t *meta, const u8 *data, size_t size);
enumError DumpNPDM (FILE *out, const u8 *data, size_t size);
enumError SaveTextNPDM (ccp filename, const u8 *data, size_t size);

#endif // LIB_NPDM_H
