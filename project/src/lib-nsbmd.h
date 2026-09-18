#ifndef LIB_NSBMD_H
#define LIB_NSBMD_H

#include "lib-model-glb.h"
#include "lib-std.h"
#include <stdint.h>
#include <stddef.h>

model_t *ParseNSBMD (const uint8_t *data, size_t size);
model_t *ParseEarlyDSBMD (const uint8_t *data, size_t size);
enumError ExportEarlyDSBMDTextures (const uint8_t *data, size_t size, const char *dest_path_or_dir);

// Runs a raw DS GX display-list blob and appends the resulting triangles
// as a new mesh on MODEL (shared with HBDF; see AppendDSGXMesh in
// lib-nsbmd.c). Returns the mesh index or -1.
int AppendDSGXMesh (
	model_t *model, const uint8_t *data, size_t size, const char *name, uint tex_w, uint tex_h);

#endif
