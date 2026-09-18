// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_HBDF_H
#define LIB_HBDF_H 1

#include "lib-nintendo.h"
#include "lib-model-glb.h"

bool IsHBDF (const u8 *data, uint size);
enumError ScanHBDF (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

// Parses DS HBDF/HSDF MDLF models (ObjectBlock/MeshBlock Nitro GX display
// lists) into a model_t. Reference: MPLibrary/DS/HBDF/* +
// ToolWrappers/HBDF/HBDF.cs (NitroGX.ReadCmds for the display lists,
// per-object transforms baked from the parent chain, one mesh per
// PolyGroup material range). SkinningBlock/EnvelopeBlock/AnimationBlock
// carry no data in the reference either and are skipped. Returns NULL
// when no mesh produced geometry.
model_t *ParseHBDF (const u8 *data, uint size);

// DecodeHBDF writes ParseHBDF's model as GLB (wmdlt path).
enumError DecodeHBDF (const u8 *data, uint size, ccp out_path);

#endif // LIB_HBDF_H
