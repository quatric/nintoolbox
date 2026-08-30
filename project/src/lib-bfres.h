#ifndef LIB_BFRES_H
#define LIB_BFRES_H

#include "lib-model-dae.h"
#include <stdint.h>
#include <stddef.h>

model_t *ParseBFRES (const uint8_t *data, size_t size);

// Switch flavour: little endian, version-major-gated header layout, a
// separate FRES-wide buffer pool (BufferInfo) index/vertex data lives in.
// See the comment above ParseBFRESSwitch() in lib-bfres.c.
model_t *ParseBFRESSwitch (const uint8_t *data, size_t size);

#endif
