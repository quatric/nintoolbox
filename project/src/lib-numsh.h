#ifndef LIB_NUMSH_H
#define LIB_NUMSH_H

#include "lib-model-glb.h"
#include <stddef.h>
#include <stdint.h>

bool IsSSBH (const uint8_t *data, size_t size);
model_t *ParseNUMSHB (const uint8_t *data, size_t size);

// Same, plus the sibling .nusktb skeleton: bone hierarchy becomes joints and
// the mesh's rigging groups become per-vertex skin weights. Pass NULL for
// SKEL to get the unskinned model ParseNUMSHB() returns.
model_t *ParseNUMSHBSkinned (
	const uint8_t *data, size_t size, const uint8_t *skel, size_t skel_size);

#endif
