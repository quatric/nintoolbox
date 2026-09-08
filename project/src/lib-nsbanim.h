#ifndef LIB_NSBANIM_H
#define LIB_NSBANIM_H

#include "lib-model-glb.h"
#include <stdint.h>
#include <stddef.h>

// Parse NSBCA (BCA0) bone/joint animation and append its tracks to model.
// Returns number of channels added, or 0 on error.
int ParseNSBCAIntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name);

// Parse NSBTA (BTA0) texture SRT animation and append to model.
int ParseNSBTAIntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name);

// Parse NSBTP (BTP0) texture pattern animation and append to model.
int ParseNSBTPIntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name);

// Parse NSBVA (BVA0) visibility animation and append to model.
int ParseNSBVAIntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name);

// Parse NSBMA (BMA0) material colour animation and append to model.
int ParseNSBMAIntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name);

// A decoded VIS0 clip (BVA0 visibility animation).  Visibility is a dense
// bitfield: bit (frame * numNode + node) of `bits` is node's visibility at
// frame, numFrame * numNode bits packed LSB-first into u32 words exactly as
// the SDK's NNSi_G3dAnmCalcNsBva indexes them.
typedef struct
{
	uint32_t num_frame;
	uint32_t num_node;
	uint32_t words; // u32 words of bitfield data
	const uint8_t *bits; // points into the caller's buffer, words * 4 bytes
} nsb_vis_t;

// Decode clip `clip_idx` of a BVA0 container into `vis`. Returns 1 on
// success, 0 on invalid data or a missing clip.
int DecodeNSBVA_Clip (nsb_vis_t *vis, const uint8_t *data, size_t size, uint32_t clip_idx);

// Visibility of one node at one frame of a decoded clip (0 or 1).
int NSBVA_Visible (const nsb_vis_t *vis, uint32_t frame, uint32_t node);

// Build a fresh one-clip BVA0 (the NDS tree layout) from a visibility
// bitfield.  Returns a malloc'd buffer (free by caller), sets *out_size.
uint8_t *BuildNSBVA (const nsb_vis_t *vis, const char *clip_name, size_t *out_size);

// BTP0 texture-pattern animation.  A BTP0 holds one NNSG3dResTexPatAnm unit
// per clip (material); each unit has a dict whose per-material entries point
// to NNSG3dResDictTexPatAnmData {numFV, flag, ratioDataFrame, offset}.  The
// values are the FV table of NNSG3dResTexPatAnmFV records, the texture and
// palette ids applied at each keyframe.  NSBTP_Lookup reproduces the SDK's
// NNSi_G3dGetTexPatAnmFV keyframe search (frame * ratioDataFrame >> 16, then
// clamped to the last keyframe whose frame <= frame).
typedef struct
{
	uint16_t frame; // keyframe
	uint8_t tex; // idTex (index into the unit's texture-name table)
	uint8_t pltt; // idPltt (0xFF = none)
} nsb_tp_key_t;

typedef struct
{
	uint32_t num_frame;
	uint32_t num_tex;
	uint32_t num_pltt;
	uint32_t num_keys; // NNSG3dResDictTexPatAnmData::numFV
	uint32_t ratio_fx16; // NNSG3dResDictTexPatAnmData::ratioDataFrame (Q16)
	const uint8_t *keys; // num_keys * 4 bytes of nsb_tp_key_t (u16 frame LE),
			     // points into the caller's buffer, sorted by frame
} nsb_tp_clip_t;

// Decode clip `clip_idx` of a BTP0 into `clip`. Returns 1 on success, 0 on
// invalid data or a missing clip.
int DecodeNSBTP_Clip (nsb_tp_clip_t *clip, const uint8_t *data, size_t size, uint32_t clip_idx);

// Keyframe the SDK applies at `frame` of a decoded clip.  Returns 1 and sets
// *tex/*pltt, or 0 when out of range or the key's ids exceed the tables.
int NSBTP_Lookup (const nsb_tp_clip_t *clip, uint32_t frame, uint32_t *tex, uint32_t *pltt);

typedef struct
{
	const char *name; // clip (material) name, up to 15 chars
	const nsb_tp_key_t *keys; // num_keys entries, sorted by frame
	uint32_t num_keys;
} nsb_tp_clip_spec_t;

// Build a fresh BTP0 from a set of clips.  Returns a malloc'd buffer (free
// by caller), sets *out_size.  Up to 15 clips.
uint8_t *BuildNSBTP (uint32_t num_frame, uint32_t num_tex, uint32_t num_pltt,
	const nsb_tp_clip_spec_t *clips, size_t num_clips, size_t *out_size);

// Encode model animations back to NSB* binary.
// Returns a malloc'd buffer (free by caller), sets *out_size. NULL on error.
uint8_t *EncodeNSBCA (const model_t *model, size_t *out_size);
uint8_t *EncodeNSBTA (const model_t *model, size_t *out_size);
uint8_t *EncodeNSBTP (const model_t *model, size_t *out_size);
uint8_t *EncodeNSBVA (const model_t *model, size_t *out_size);
uint8_t *EncodeNSBMA (const model_t *model, size_t *out_size);

// Scan a directory for NSB* animation files that are siblings of the given
// NSBMD path and merge them into the model.  The directory layout expected
// is the flat NDS style: <dir>/<name>.nsbmd alongside <dir>/<name>.nsbca etc.
void ImportNSBAnimSiblings (model_t *model, const char *nsbmd_path);

#endif
