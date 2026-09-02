#ifndef LIB_NUMSH_H
#define LIB_NUMSH_H

#include "lib-model-glb.h"
#include <stddef.h>
#include <stdint.h>

bool IsSSBH (const uint8_t *data, size_t size);
model_t *ParseNUMSHB (const uint8_t *data, size_t size);

#endif
