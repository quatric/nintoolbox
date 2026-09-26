// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// TT Games NTT engine `.model` models (LEGO Star Wars: The Skywalker Saga).
//
// Chunked container of big-endian resource chunks, each holding a 12-byte
// type tag (`.CC4HSERHSER` hierarchy, `.CC4HSER2CSG` scene data) with all
// vertex/index payloads in little-endian DXTV buffers. Layout ported from
// KillzXGaming/NTT-Model-Dumper's Model.cs (no license file upstream;
// algorithm re-implemented here in C, GLB export via this project's own
// model_t instead of IONET/DAE).
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_NTTMODEL_H
#define SZS_LIB_NTTMODEL_H 1

#include "types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#include "lib-model-glb.h"

	bool IsTTModel (const u8 *data, size_t size);
	model_t *ParseTTModel (const u8 *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
