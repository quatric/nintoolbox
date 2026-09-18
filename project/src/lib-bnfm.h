// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_BNFM_H
#define LIB_BNFM_H 1

#include "types.h"
#include "lib-model-glb.h"

// Decode a Nintendo / Nd Cube BNFM 3D model (Animal Crossing: Amiibo Festival,
// Mario Party 10, Wii U Party) to a GLB or COLLADA .dae file.
enumError DecodeBNFM (const u8 *data, uint size, ccp out_path);
enumError EncodeModelToBNFM (const model_t *model, ccp out_path);

// Parses a BNFM skeletal animation blob (MPLibrary/WiiU/BNFM/Animation/
// BNFMSA.cs: 10 SRT tracks per bone, Normal/Hermite keys) and appends it
// to MODEL's animations, matching bone groups to joints by name. Times are
// frames/60 like the rest of this codebase; Hermite slopes are read but
// sampled linearly, exactly like the reference (which drops them when
// building keyframes). Returns the number of channels added.
int AppendBNFMSAAnimation (model_t *model, const u8 *data, uint size);

// DecodeBNFM plus an animation sidecar: a sibling .bnfmsa file holding a
// BNFMSA blob for the same skeleton (may be NULL).
enumError DecodeBNFMWithAnim (
	const u8 *data, uint size, const u8 *anim_data, uint anim_size, ccp out_path);

#endif // LIB_BNFM_H
