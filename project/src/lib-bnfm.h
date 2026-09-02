// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_BNFM_H
#define LIB_BNFM_H 1

#include "types.h"
#include "lib-model-glb.h"

// Decode a Nintendo / Nd Cube BNFM 3D model (Animal Crossing: Amiibo Festival,
// Mario Party 10, Wii U Party) to a GLB or COLLADA .dae file.
enumError DecodeBNFM (const u8 *data, uint size, ccp out_path);

#endif // LIB_BNFM_H
