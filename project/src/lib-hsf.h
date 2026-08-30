// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// HAL Laboratory "HSFV037" model format (Mario Party 4-8 .hsf, extracted from
// the MPBIN container by wmpbdump; the same tool family HAL used across
// Kirby Air Ride, Battalion Wars and other GameCube/Wii titles).
//
// See the comment above DecodeHSF() in lib-hsf.c for the AttributeHeader
// table layout and what is/isn't decoded.
//-----------------------------------------------------------------------------

#ifndef LIB_HSF_H
#define LIB_HSF_H

#include "types.h"
#include "lib-model-dae.h"

// Recognise and decode an HSFV037 model's geometry, object hierarchy,
// materials and native GX textures, one mesh per named mesh-object, single
// part or multi-part alike, to a model file beside 'out_path'. Textures are
// decoded to sibling PNG files and referenced/embedded by DAE/GLB. The format
// is chosen by 'out_path's extension: GLB by default, or COLLADA .dae if
// 'out_path' ends in ".dae". Materials, textures and bone/skin-weight
// CENV single-, double- and multi-bone envelope weights are exported with
// inverse bind matrices. The complete motion table is decoded losslessly to
// OUT_PATH.motion.json (targets/effects/interpolation/constants/keyframes),
// while transform and morph curves are also synthesized as native GLB
// animation channels. Shape and cluster buffers become GLB morph targets.
// Returns
// ERR_NOTHING_TO_DO if 'data' isn't an HSFV037 file, or
// if it uses a primitive record type other than triangle/quad that this
// decoder does not yet support -- see lib-hsf.c.
enumError DecodeHSF (const u8 *data, uint size, ccp out_path);

// Encode a portable static HSFV037 model. Geometry, normals, UVs, vertex
// colors, triangle materials, material names and the joint/object hierarchy
// are written in Hudson's native big-endian section layout. Runtime-only
// features which model_t cannot express are intentionally omitted.
enumError EncodeModelToHSF (const model_t *model, ccp out_path);

#endif // LIB_HSF_H
