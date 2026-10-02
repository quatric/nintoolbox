#ifndef LIB_ATLUS_NDX_H
#define LIB_ATLUS_NDX_H

#include "file-type.h"
#include "dclib-types.h"

// Atlus Nintendo DS / 3DS Archive System (*.ndx, *.idx, *.bin;
// Radiant Historia, Shin Megami Tensei: Strange Journey / Devil Survivor,
// Etrian Odyssey; Nintendo DS / Nintendo 3DS)
//
// Format layout:
//
// 1. *.ndx (Name Directory Index)
//    - +0x00..+0x01: u16 root entry count N (little-endian)
//    - Followed by N directory/file entries:
//        u16 name_len;
//        char name[name_len]; // ASCII string, not null-terminated
//        u32 child_offset;    // offset to child directory block, or 0 if leaf
//    - Child blocks start with u16 child_count, followed by child_count entries.
//
// 2. *.idx (Hash Lookup Index)
//    - +0x00..+0x01: u16 bucket count B (typically 2048 / 0x800)
//    - +0x02..+0x07: 6 zero padding bytes
//    - +0x08..+0x08 + B * 6: Array of B bucket records (6 bytes each)
//        u32 w0;
//        u16 w4;
//      default_size = (((u32)w4 << 16) >> 10) + (w0 >> 26);
//      If (w0 & 1) == 0 (direct entry):
//        bin_offset = (((w0 << 6) & 0xffffffff) >> 7) << 2;
//        bin_size = default_size;
//      If (w0 & 1) != 0 (collision list):
//        idx_offset = ((w0 << 6) & 0xffffffff) >> 7;
//        At idx_offset:
//          u8 item_count;
//          For item_idx = 0 .. item_count - 1:
//            u32 bin_offset = (*(u32*)(idx + pos)) << 2;
//            u32 bin_size = (item_idx == 0) ? default_size : *(u32*)(idx + pos + 4);
//            Followed by match condition pairs:
//              while ((ch = idx[chk++]) != 0) { u8 str_pos = idx[chk++]; ... }
//
// 3. *.bin (Payload Archive)
//    - Raw concatenated payload files at bin_offset with length bin_size.

bool IsAtlusNdx (const u8 *data, uint data_size, u64 file_size);

bool AtlusIdxLookup (
	const u8 *idx_data,
	size_t idx_size,
	const char *path,
	u32 *out_offset,
	u32 *out_size
);

#endif // LIB_ATLUS_NDX_H
