#ifndef LIB_NSBMD_H
#define LIB_NSBMD_H

#include "lib-model-glb.h"
#include "lib-std.h"
#include <stdint.h>
#include <stddef.h>

model_t *ParseNSBMD (const uint8_t *data, size_t size);
model_t *ParseEarlyDSBMD (const uint8_t *data, size_t size);
enumError ExportEarlyDSBMDTextures (const uint8_t *data, size_t size, const char *dest_path_or_dir);

#endif
