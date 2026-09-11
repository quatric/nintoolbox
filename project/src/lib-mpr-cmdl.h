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
// Static meshes only in this pass: skinned SMDL (SKHD, versions
// 127/133) is declined, as is material/texture resolution (meshes
// export untextured with their retail material index preserved in
// the mesh name).
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
