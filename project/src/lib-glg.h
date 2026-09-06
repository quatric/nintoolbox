// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Next Level Games GLG model format (Super Mario Strikers, GameCube).
//
// Format reverse engineered from the retail asset corpus plus the runtime
// loader (FUN_801bfdf0) and display-list builder (FUN_801c1e5c) in the
// game's main.dol, decompiled in Ghidra. A big-endian chunked container:
//
//   outer chunk: tag=0x8001b0{00|01}00.., u32 length, then inner chunks back
//   to back until length bytes are consumed. Each inner chunk is
//   { u32 tag (top bit + 7-bit alignment-shift + 24-bit key), u32 length,
//   payload }. Keys seen in the geometry-only (non-map, non-skeleton) files:
//
//     0x1b001  4 bytes, opaque (version/flags, preserved verbatim)
//     0x1b002  64 bytes, a 4x4 float matrix (root transform)
//     0x1b003  model table, 16 bytes/entry: {meshCount:u32, hash:u32, u64 pad}
//     0x1b004  mesh table, 74 (0x4a) bytes/entry -- see glg_mesh_entry_t
//     0x1b005  vertex-attribute descriptors (VAPD), 6 bytes/entry:
//              {u16 reserved, u16 byte_offset_in_vertex_chunk, u8 attr_index,
//              u8 attr_type}. mesh_entry.vapd_off/attribute_count select the
//              contiguous run of records for that mesh.
//     0x1b006  vertex data: one packed array per attribute, back to back at
//              the VAPD-declared offsets. Per-attribute element stride is
//              derived from the gap to the next attribute's offset (or to
//              the end of the chunk for the last one) -- confirmed to divide
//              evenly by a common per-mesh vertex count across every sample.
//     0x1b007  index data: flat u16 array. mesh_entry.face_off/face_count
//              (byte offset + u16 count) select the contiguous run for that
//              mesh; a single index selects the same element from every
//              attribute array (GX multi-array/shared-index convention).
//
// Only decode is implemented so far -- byte-exact encode is unimplemented.
//-----------------------------------------------------------------------------

#ifndef LIB_GLG_H
#define LIB_GLG_H

#include "types.h"
#include "lib-model-glb.h"

// Decode a Next Level Games .glg model to GLB. Handles the non-skeletal,
// single-model file layout (character/prop meshes); map containers
// (tag 0x8001b100, modelCount > 50) and external .shier skeletons are not
// supported.
enumError DecodeGLG (const u8 *data, uint size, ccp out_glb_path);

// Same, but SRC_PATH lets the decoder find the sibling .glt/.rlt PTLG
// container a .glg binds its textures from by hash. Pass NULL to skip
// texture binding entirely.
enumError DecodeGLG2 (const u8 *data, uint size, ccp src_path, ccp out_glb_path);

// Encode a parsed model (GLB input, as produced by DecodeGLG) as a
// geometry-only .glg file -- the inverse of DecodeGLG. Positions are written
// as 10.6 fixed-point s16 and texcoords as u16/1024; mesh-table fields with
// no recovered meaning are zero-filled, so this does not reproduce a retail
// file byte for byte, but encode -> GLB -> encode is a byte-exact fixed
// point.
enumError EncodeGLG (const model_t *model, ccp out_path);

#endif // LIB_GLG_H
