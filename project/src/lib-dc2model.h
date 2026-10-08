// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "DC2" container models (.dcm; Speed 2, Wii). Big-endian floats, written
// after a descriptor header. Vertex arrays are length-prefixed blocks
//   {u32 0, u8 stride, u32 count, data}: positions (stride 12), normals
// (stride 12) and texture coordinates (stride 8), followed by a stream of
// triangle-strip records {u8 0x9b, u16 count, count * {u16 position,
// u16 normal, u16 texcoord}}. Strip winding verified against the stored
// normals. Only files with a single vertex-array set are decoded.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_DC2MODEL_H
#define SZS_LIB_DC2MODEL_H 1

#include "lib-std.h"
#include "lib-model-glb.h"

bool IsDC2Model (const u8 *data, size_t size);
// Returns a malloc'ed model_t (free with FreeModel) or NULL.
model_t *ParseDC2Model (const u8 *data, size_t size);

#endif
