// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_CNMT_H
#define LIB_CNMT_H

#include "lib-nintendo.h"

// Nintendo Switch Content Metadata (.cnmt)
// Official format defined in NintendoSDK (Siglo) nn/ncm/ncm_ContentMeta.h

typedef struct cnmt_content_info_t
{
	u8 nca_id[16];
	u64 size;
	u8 content_type;
	u8 id_offset;
} cnmt_content_info_t;

typedef struct cnmt_header_t
{
	u64 title_id;
	u32 version;
	u8 type;
	u8 attributes;
	u16 extended_header_size;
	u16 content_count;
	u16 content_meta_count;
	u32 required_download_system_version;
} cnmt_header_t;

bool IsCNMT (const u8 *data, size_t size);
enumError DumpCNMT (FILE *out, const u8 *data, size_t size);
enumError SaveTextCNMT (ccp filename, const u8 *data, size_t size);

#endif // LIB_CNMT_H
