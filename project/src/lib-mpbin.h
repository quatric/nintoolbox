// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_MPBIN_H
#define LIB_MPBIN_H 1

#include "lib-nintendo.h"

#define MPBIN_COMP_NONE       0
#define MPBIN_COMP_LZSS       1
#define MPBIN_COMP_SLIDE      2
#define MPBIN_COMP_FSLIDE_ALT 3
#define MPBIN_COMP_FSLIDE     4
#define MPBIN_COMP_RLE        5
#define MPBIN_COMP_INFLATE    7

extern ccp MPBIN_SETUP_FILE;

bool IsMPBIN (const u8 *data, uint size);
// Inflate-only MPBIN and MDR share the same chunk-header layout.
bool IsMPBINInflate (const u8 *data, uint size);
enumError ScanMPBIN (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);
enumError CreateMPBIN (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

bool looks_like_mpbin_dir (ccp dir);
enumError create_mpbin_dir (ccp source, ccp dest);

// Compression & Decompression
enumError DecompressMPBIN_LZSS (u8 *dst, uint dst_len, const u8 *src, uint src_len);
enumError DecompressMPBIN_Slide (u8 *dst, uint dst_len, const u8 *src, uint src_len);
enumError DecompressMPBIN_RLE (u8 *dst, uint dst_len, const u8 *src, uint src_len);
enumError DecompressMPBIN_Inflate (u8 *dst, uint dst_len, const u8 *src, uint src_len);

enumError CompressMPBIN_LZSS (u8 **dest, uint *dest_size, const u8 *src, uint src_len);
enumError CompressMPBIN_Slide (u8 **dest, uint *dest_size, const u8 *src, uint src_len);
enumError CompressMPBIN_RLE (u8 **dest, uint *dest_size, const u8 *src, uint src_len);
enumError CompressMPBIN_Inflate (u8 **dest, uint *dest_size, const u8 *src, uint src_len);

#endif // LIB_MPBIN_H
