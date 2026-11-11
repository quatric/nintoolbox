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

#endif // SZS_LIB_PHIVE_H
