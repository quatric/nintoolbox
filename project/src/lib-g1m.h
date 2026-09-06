// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_G1M_H
#define LIB_G1M_H

#include "types.h"
#include "lib-model-glb.h"

// Decode a Koei Tecmo G1M model to GLB. Geometry only: positions and
// triangles. The vertex attribute table's type and semantic encodings are
// not established, so normals, UVs and skin weights are not exported.
enumError DecodeG1M (const u8 *data, uint size, ccp out_glb_path);

#endif
