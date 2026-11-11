#ifndef SZS_LIB_PHIVE_H
#define SZS_LIB_PHIVE_H 1

#include "lib-std.h"

// Phive collision shapes (".bphsh"), the Havok-based physics format of Splatoon 3 (and later Nintendo games).
//
// A .bphsh is a small little-endian header ("Phive\0"), a big-endian Havok tag file (TAG0) that holds one
// hknpMeshShape in its in-memory layout (relative arrays/pointers, 16 bit quantized vertices in sections of
// up to 256 vertices), and two material tables (material id + flags, collision-disable flags).
//
// Layout and the Splatoon 3 name tables after nvnprogram/FiveX and nvnprogram/PhiveConverter (GPL-2.0).

// True if 'data' starts like a Phive file.
bool IsPhiveFile (const u8 *data, size_t size);

// Convert a .bphsh to a Wavefront OBJ (every triangle with its own three vertices, one "o"/"usemtl" group per
// material entry) and the material info JSON (material name, material flag names, collision-disable flag
// names, keyed by the OBJ group name). Both outputs are allocated with MALLOC; free them with FREE.
enumError ConvertPhiveToObj (
	const u8 *data, size_t size, char **obj, size_t *obj_size, char **json, size_t *json_size);

// Static compound bodies (".bphsc", Phive container version 2.2 as in Mario Kart World): every Havok tag file in
// the container holds an hknpCompoundShape whose instances (rotation, translation, scale) place mesh, convex and
// capsule shapes, possibly through nested compounds. IsPhiveCompound() checks the container version.
bool IsPhiveCompound (const u8 *data, size_t size);

// Write all instanced shapes as one Wavefront OBJ in world space ("g compound_N" per tag file, "o N_<type>" per
// shape, "usemtl material_<tag>" for mesh triangles; capsules as 8-sided tubes). ERR_NOTHING_TO_DO for bodies
// without shapes (empty bodies and water grids). The output is allocated with MALLOC; free it with FREE.
enumError ConvertPhiveCompoundToObj (const u8 *data, size_t size, char **obj, size_t *obj_size);

// Water grid of a static compound body: MALLOC'd w * h cells of 4 u16 {height, flow x, flow z, flags} (height
// spans the box height; flow 0x7fff = still), box = {size x, y, z, center x, y, z}. ERR_NOTHING_TO_DO if none.
enumError DecodePhiveWaterGrid (const u8 *data, size_t size, u16 **cells, uint *width, uint *height, float box[6]);

#endif // SZS_LIB_PHIVE_H
