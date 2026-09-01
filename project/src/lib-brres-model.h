#ifndef LIB_BRRES_MODEL_H
#define LIB_BRRES_MODEL_H

#include <stddef.h>
#include <stdint.h>
#include "lib-model-glb.h"

model_t *ParseMDL0 (const uint8_t *data, size_t size);
void FreeModel (model_t *model);

// Parse a CHR0 (bone animation) sub-file's raw bytes and append its joint
// TRS channels to `model`, matching bone entries by name. Returns 1 if at
// least one channel was added, 0 otherwise (not a CHR0, no matching bones).
int ParseCHR0IntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name);

#endif
