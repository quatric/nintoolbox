#ifndef LIB_NUD_H
#define LIB_NUD_H

#include "lib-model-glb.h"
#include <stddef.h>
#include <stdint.h>

bool IsNUD (const uint8_t *data, size_t size);
model_t *ParseNUD (const uint8_t *data, size_t size);
int EncodeModelToNUD (const model_t *model, const char *out_nud_path);

#endif
