// SPDX-License-Identifier: GPL-2.0+
// Capcom MT Framework Mobile (Nintendo 3DS: Resident Evil Revelations /
// Mercenaries 3D, Monster Hunter 3U/4 era) model/texture/material/shader
// files, plus the ModelBinary (.mbn) companion format.
//
// Reference: SPICA by gdkchan (https://github.com/gdkchan/SPICA) and the
// 3dsTools CLI wrapper by KillzXGaming
// (https://github.com/KillzXGaming/3dsTools), which converts these formats
// through the SPICA library. Magic numbers, header layouts, the skeleton,
// mesh, strip-index and vertex-attribute decoding and the MBN buffer pairing
// below are ported from that library's SPICA/Formats/MTFramework and
// SPICA/Formats/ModelBinary trees.
#ifndef LIB_MTMOB_H
#define LIB_MTMOB_H 1

#include "lib-std.h"

// Structural probes (magic + bounds validation).
int IsMTMOD (const u8 *data, size_t size); // "MOD\0" model
int IsMTTEX (const u8 *data, size_t size); // "TEX\0" texture
int IsMTMRL (const u8 *data, size_t size); // "MRL\0" materials
int IsMTMFX (const u8 *data, size_t size); // "MFX\0" shader effects
int IsMBN (const u8 *data, size_t size); // ModelBinary (no magic, .mbn)

// MT TEX (.tex / "TEX\0") -> RGBA8. The payload is already PICA200-ordered,
// so this unwraps the Word0/Word1/Word2 geometry header and reuses
// DecodePicaTexture.
enumError DecodeMTTEX_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, size_t size);

// MT MOD ("MOD\0") -> model_t (lib-model-glb.h), typed as void* here so
// this header stays independent of the model layer. Release with FreeModel().
// The skeleton always decodes; mesh vertices decode through the vertex
// layout of a sibling .mfx/.lfx shader (SPICA needs the same file) or,
// when none is found, through validated common float layouts. Material and
// texture names resolve through a sibling .mrl when present. Pass NULL for
// sibling_dir to skip the sibling search (used by tests).
void *ParseMTMOD (const u8 *data, size_t size, ccp sibling_dir);

// MBN + companion BCH -> model_t. The MBN carries replacement vertex/index
// buffers for the base scene's meshes (SPICA MBn.ToH3D); bone names and
// materials come from the BCH. Meshes pair in order. Returns NULL without a
// usable base scene.
void *ParseMBN (const u8 *data, size_t size, const u8 *bch_data, size_t bch_size);

// Human-readable manifests for `wszst xx` (see extract_*_manifest pattern).
enumError DecodeMTMRL_Text (FILE *f, const u8 *data, size_t size);
enumError DecodeMTMFX_Text (FILE *f, const u8 *data, size_t size);
enumError DecodeMBN_Text (FILE *f, const u8 *data, size_t size);
enumError DecodeMTMOD_Text (FILE *f, const u8 *data, size_t size);

#endif // LIB_MTMOB_H
