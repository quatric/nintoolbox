// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// PlatinumGames WMB models (Star Fox Zero, Wii U, big-endian `\0BMW`).
//
// Header, mesh/batch tables, vertex layouts and bone tables per
// Kerilk/noesis_bayonetta_pc (Bayo.h: bayoWMBHdr/wmbMesh/wmbBatch,
// re-implemented); only the seven (vertexFormat, numMapping,
// unknownD) combinations present in the 627-file retail corpus are
// accepted. Static geometry in this pass (positions + normals + UVs
// + colours); bone parents/positions decode for a later skinning
// pass, tangent sets are parsed but not exported.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_WMB_H
#define SZS_LIB_WMB_H 1

#include "lib-nintendo.h"
#include "lib-model-glb.h"

bool IsPlatinumWMB (const u8 *data, uint size);
model_t *ParsePlatinumWMB (const u8 *data, size_t size);

#endif
