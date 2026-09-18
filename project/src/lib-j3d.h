#ifndef LIB_J3D_H
#define LIB_J3D_H

#include "lib-model-glb.h"
#include "lib-std.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

// J3D BMD/BDL -- GameCube/Wii binary model format (SuperBMD feature parity).
//
// Decode (BMD/BDL -> model_t -> GLB) preserves geometry (positions, normals,
// 2 vertex-color channels, 8 UV channels), skinning (joints, weights,
// inverse-bind matrices), materials (diffuse color, texture bindings, wrap
// modes) and textures (all GX formats incl. mipmaps, staged as PNG).
// Materials are intentionally simplified to model_t's representation; the
// full GX TEV pipeline is preserved in the sidecar JSON files, not in GLB.
//
// Encode (model_t -> BMD/BDL) rebuilds every section from scratch with a
// canonical single-TEV-stage material per model material (like SuperBMD's
// SetUpTev), RGBA32 or CMPR textures, and plain GX triangle lists.

// --- detection (magic + size sanity) ---
int IsJ3D (const uint8_t *data, size_t size); // J3D2 + bmd3/bmd2/bdl4
int IsJ3DBMD (const uint8_t *data, size_t size); // J3D2bmd3 or legacy bmd2
int IsJ3DBDL (const uint8_t *data, size_t size); // J3D2bdl4

// --- decode ---
model_t *ParseJ3D (const uint8_t *data, size_t size);

// Stage every TEX1 texture as "<model-dir>/<name>.png" (+ "<name>_mipN.png"
// for mip levels), like the BRRES path in wmdlt.c. Returns staged count.
int ExportJ3DTexturesFromData (const uint8_t *data, size_t size, const char *dest_glb);

// SuperBMD-style sidecars, written next to the decoded GLB by wmdlt.
// Simplified schemas (documented in docs/FORMATS.md); --mat/--texheader
// counterparts are consumed on encode.
int ExportJ3DMaterialsJSON (const uint8_t *data, size_t size, const char *json_path);
int ExportJ3DTexHeadersJSON (const uint8_t *data, size_t size, const char *json_path);

// --- info ---
void J3DProfileDump (const uint8_t *data, size_t size, FILE *f);

// --- encode options (mirrors SuperBMD CLI) ---
typedef struct j3d_encode_opt_t
{
	ccp mat_path; // --mat: material JSON applied on encode (NULL = default)
	ccp texheader_path; // --texheader: texture header JSON (NULL = default)
	ccp tex_dir; // extra directory to search for textures (NULL = input dir)
	int tristrip; // 0=none 1=static 2=all (default 1; v1 emits lists, see docs)
	int rotate_model; // --rotate: Z-up -> Y-up (x,y,z)->(x,z,-y)
	int tex_float32; // --texfloat32: force f32 UVs
	int degenerate; // --degeneratetri: bridging degenerates (reserved)
	int no_mipmaps; // --nomipmaps: ignore _mipN files
	int is_bdl; // --bdl or .bdl dest: write BDL + stub MDL3
} j3d_encode_opt_t;

void SetupDefaultJ3DEncodeOpt (j3d_encode_opt_t *opt);

// GLB/model -> BMD (is_bdl=0) or BDL (is_bdl=1).
enumError EncodeModelToJ3D (const model_t *model, const char *out_path,
	const j3d_encode_opt_t *opt);

#endif
