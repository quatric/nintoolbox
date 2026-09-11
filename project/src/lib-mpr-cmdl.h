// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Metroid Prime Remastered CMDL models (Switch).
//
// LE RFRM form ("CMDL" id, reader/writer versions 114/125) holding HEAD
// (bounds), MTRL (materials), MESH (render-mesh table), VBUF/IBUF
// (vertex/index buffer descriptors), GPU (LZSS mode 0-3 buffers) chunks,
// plus a trailing FOOT form whose META table locates each buffer.
// Layout per PrimeDecomp/retrotool's cmdl.rs (MIT/Apache-2.0,
// re-implemented) and verified against 687 retail CMDL members.
// Static CMDL (114/125) and skinned SMDL (127/133, SKHD chunk):
// SMDL decodes unskinned here — bone transforms live outside either
// file (no skeleton asset is known) — but its skinning data is parsed
// and validated, never trusted blindly: SKHD field 1 is the bone
// count (every BoneIndices u8x4 value is below it corpus-wide),
// BoneWeights (half-x4 or float-x4) rows must be finite and sum to
// ~=1, and meshes failing either are skipped like any other corrupt
// geometry. MTRL material names are exported and bound per-mesh;
// texture-uuid resolution is future work.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_MPR_CMDL_H
#define SZS_LIB_MPR_CMDL_H 1

#include "types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#include "lib-model-glb.h"

bool IsMPRCMDL (const u8 *data, uint size);
model_t *ParseMPRCMDL (const u8 *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
