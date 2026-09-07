#ifndef LIB_NSBANIM_H
#define LIB_NSBANIM_H

#include "lib-model-glb.h"
#include <stdint.h>
#include <stddef.h>

// Parse NSBCA (BCA0) bone/joint animation and append its tracks to model.
// Returns number of channels added, or 0 on error.
int ParseNSBCAIntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name);

// Parse NSBTA (BTA0) texture SRT animation and append to model.
int ParseNSBTAIntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name);

// Parse NSBTP (BTP0) texture pattern animation and append to model.
int ParseNSBTPIntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name);

// Parse NSBVA (BVA0) visibility animation and append to model.
int ParseNSBVAIntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name);

// Parse NSBMA (BMA0) material colour animation and append to model.
int ParseNSBMAIntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name);

// Encode model animations back to NSB* binary.
// Returns a malloc'd buffer (free by caller), sets *out_size. NULL on error.
uint8_t *EncodeNSBCA (const model_t *model, size_t *out_size);
uint8_t *EncodeNSBTA (const model_t *model, size_t *out_size);
uint8_t *EncodeNSBTP (const model_t *model, size_t *out_size);
uint8_t *EncodeNSBVA (const model_t *model, size_t *out_size);
uint8_t *EncodeNSBMA (const model_t *model, size_t *out_size);

// Scan a directory for NSB* animation files that are siblings of the given
// NSBMD path and merge them into the model.  The directory layout expected
// is the flat NDS style: <dir>/<name>.nsbmd alongside <dir>/<name>.nsbca etc.
void ImportNSBAnimSiblings (model_t *model, const char *nsbmd_path);

#endif
