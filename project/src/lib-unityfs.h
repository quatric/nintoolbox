// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Unity "UnityFS" asset bundles (.unity3d, .bundle, .assetbundle, .ab, ...),
// the container used by Unity 5.3+ (see AssetRipper's BundleFile reader).
// Header fields are big-endian:
//
//   "UnityFS\0"  u32 version  cstr player_version  cstr engine_version
//   i64 total_size  u32 blockinfo_comp_size  u32 blockinfo_uncomp_size
//   u32 flags  (bits 0-5: block-info compression 0 none/1 LZMA/2 LZ4/3 LZ4HC,
//               0x40: directory is stored inside the block info,
//               0x80: block info is at the end of the file,
//               0x100: old-web padding, 0x200: block info aligned to 16)
//
// Block info: 16-byte hash, u32 block_count, {u32 uncomp, u32 comp, u16 flags}
// per block, u32 node_count, {i64 offset, i64 size, u32 flags, cstr path} per
// node.  Block data follows the (optionally aligned) header; the nodes are
// slices of the concatenated decompressed blocks (serialized "CAB-*" files,
// ".resS" streaming data and ".resource" audio payloads).
//
// Members are extracted raw; encrypted bundles (flags 0x400 on blocks or
// header) are rejected.  Serialized-file object decoding is out of scope.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_UNITYFS_H
#define SZS_LIB_UNITYFS_H 1

#include "lib-nintendo.h"

bool IsUnityFS (const u8 *data, size_t size);

// Decompress all blocks and return the node list in 'entries'
// (free with ResetOwnedEntries()).
enumError ScanUnityFS (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif
