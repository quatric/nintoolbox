// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_NRO_H
#define LIB_NRO_H

#include "lib-nintendo.h"

// Nintendo Switch Executable (.nro / NRO0)
// Official format defined in NintendoSDK (Siglo) ro_NroHeader.h and Homebrew ASET trailer.

#define NRO0_MAGIC 0x304F524E // "NRO0"
#define ASET_MAGIC 0x54455341 // "ASET"

typedef struct nro_segment_t
{
	u32 offset;
	u32 size;
} nro_segment_t;

typedef struct nro_header_t
{
	u32 entrypoint_insn;
	u32 mod0_offset;
	u8 padding[8];
	u32 magic; // "NRO0"
	u32 version;
	u32 size;
	u32 flags;
	nro_segment_t text;
	nro_segment_t ro;
	nro_segment_t data;
	u32 bss_size;
	u32 reserved;
	u8 module_id[32];
	u32 dso_handle_offset;
	u32 reserved2;
	nro_segment_t embedded_aset;
} nro_header_t;

typedef struct nro_asset_section_t
{
	u64 offset;
	u64 size;
} nro_asset_section_t;

typedef struct nro_aset_header_t
{
	u32 magic; // "ASET"
	u32 version;
	nro_asset_section_t icon;
	nro_asset_section_t nacp;
	nro_asset_section_t romfs;
} nro_aset_header_t;

bool IsNRO (const u8 *data, size_t size);
enumError ParseNROHeader (nro_header_t *hdr, const u8 *data, size_t size);
enumError ExtractNROArchive (ccp arg, ccp basedir, uint depth);
enumError DumpNRO (FILE *out, const u8 *data, size_t size);
enumError SaveTextNRO (ccp filename, const u8 *data, size_t size);

#endif // LIB_NRO_H
