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

// Parse NSBCK (BCK0) character animation and append to model.
int ParseNSBCKIntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name);

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

// BMA0 material-colour animation and BTA0 texture-SRT animation.  Both reuse
// the BTP0 container scaffolding (outer clip-name dict + a unit holding an
// inner keyframe dict with stride-12 entries and a ratioDataFrame-seeded
// keyframe search), so decoding and the lookup walk are the same; only the
// per-keyframe payload differs.  The payload layouts below follow the
// NNSG3dResMatCAnm / NNSG3dResTexSRTAnm model: BMA0 keys carry five packed
// colours (diffuse, ambient, specular, emission, polygon-alpha) and BTA0 keys
// carry five real values (scale X/Y, rotation, translation X/Y).  Note: this
// reconstruction is built for byte-exact round-tripping and validated by
// synthetic tests; it was verified structurally but not byte-for-byte against
// a retail animation.
#define NSB_MATCOL_CHANNELS 5
typedef struct
{
	uint16_t frame; // keyframe
	uint32_t color[NSB_MATCOL_CHANNELS]; // 0xaarrggbb each
} nsb_ma_key_t;

typedef struct
{
	uint32_t num_frame;
	uint32_t num_channels; // NSB_MATCOL_CHANNELS
	uint32_t num_keys;
	uint32_t ratio_fx16;
	const uint8_t *keys; // num_keys * 22 bytes of nsb_ma_key_t, sorted by frame
} nsb_ma_clip_t;

typedef struct
{
	const char *name;
	const nsb_ma_key_t *keys; // num_keys entries, sorted by frame
	uint32_t num_keys;
} nsb_ma_clip_spec_t;

int DecodeNSBMA_Clip (nsb_ma_clip_t *clip, const uint8_t *data, size_t size, uint32_t clip_idx);

// Linearly interpolate one channel's colour between the surrounding keys of a
// decoded clip.  Returns 1 and sets *color, or 0 when frame/channel is out of
// range.
int NSBMA_Lookup (const nsb_ma_clip_t *clip, uint32_t channel,
	uint32_t frame, uint32_t *color);

uint8_t *BuildNSBMA (uint32_t num_frame, const nsb_ma_clip_spec_t *clips,
	size_t num_clips, size_t *out_size);

#define NSB_TEXSRT_PARAMS 5
typedef struct
{
	uint16_t frame; // keyframe
	int16_t v[NSB_TEXSRT_PARAMS]; // scale X, scale Y, rotation, trans X, trans Y (fx1.10.5)
} nsb_ta_key_t;

typedef struct
{
	uint32_t num_frame;
	uint32_t num_keys;
	uint32_t ratio_fx16;
	const uint8_t *keys; // num_keys * 12 bytes of nsb_ta_key_t, sorted by frame
} nsb_ta_clip_t;

typedef struct
{
	const char *name;
	const nsb_ta_key_t *keys; // num_keys entries, sorted by frame
	uint32_t num_keys;
} nsb_ta_clip_spec_t;

int DecodeNSBTA_Clip (nsb_ta_clip_t *clip, const uint8_t *data, size_t size, uint32_t clip_idx);

// Linearly interpolate the five SRT parameters between the surrounding keys of
// a decoded clip.  Returns 1 and fills v[], or 0 when frame is out of range.
int NSBTA_Lookup (const nsb_ta_clip_t *clip, uint32_t frame,
	int16_t v[NSB_TEXSRT_PARAMS]);

uint8_t *BuildNSBTA (uint32_t num_frame, const nsb_ta_clip_spec_t *clips,
	size_t num_clips, size_t *out_size);

// BCK0 (CHR0) character (inverse-TRS) animation.  Each clip unit animates a
// named skeleton: per node a 3x3 basis matrix (row-major) plus a translation
// vector.  An NNSG3dResChrAnm "C" unit carries per-node data with, for each
// of the 9 basis cells and 3 position scalars, one of three axis modes:
// identity/zero, a single constant fx32, or a per-frame stream.  Streams use
// the JNT0-style step2/step4 / fx16 codec shared with NSBCA.  The basis is a
// full 3x3 matrix (not SRT-decomposable), so CHR0 has no glTF form here and
// only round-trips as preserved raw bytes.  Note: the on-disk layout below is
// a documented reconstruction (kind/basis/pos structure verified against the
// BCA0 sibling codec, not byte-for-byte against a retail animation).
#define NSB_CHR_BASIS_CELLS 9
#define NSB_CHR_POS_AXES 3
#define NSB_CHR_AXES 12 // basis cells 0..8, position scalars 9..11

typedef enum
{
	NSB_CHR_AXIS_NONE = 0, // identity basis cell / zero position scalar
	NSB_CHR_AXIS_CONST,    // single fx32 value
	NSB_CHR_AXIS_ANIM      // per-frame fx32 stream (see step)
} nsb_chr_axis_mode_t;

typedef struct
{
	const char *name; // node name (skeleton bone), up to 15 chars
	uint8_t mode[NSB_CHR_AXES]; // per-axis NSB_CHR_AXIS_*
	int32_t row_major[9]; // fx32 const basis matrix (used per CONST axis)
	int32_t pos[3]; // fx32 const position (used per CONST axis)
	// ANIM keys are per-frame fx32 samples (at least num_frame entries);
	// step selects how they are compacted into the stream:
	//   1 = one per frame, 2 = one per frame pair, 4 = one per quad frame.
	const int32_t *keys[NSB_CHR_AXES];
	uint8_t step[NSB_CHR_AXES]; // 1, 2 or 4 (ignored unless ANIM)
} nsb_ck_node_spec_t;

typedef struct
{
	const char *name; // clip name, up to 15 chars
	const nsb_ck_node_spec_t *nodes; // num_nodes entries
	uint32_t num_nodes;
} nsb_ck_clip_spec_t;

typedef struct
{
	uint32_t num_frame;
	uint32_t num_node;
	const uint8_t *unit; // clip unit base (resolves unit-relative offsets)
	uint32_t unit_len; // unit buffer length (bounds the offsets above)
	const uint8_t *names; // num_node * 16-byte NNSG3dResName
	const uint8_t *data; // num_node * 12 bytes (info, ofsBasis, ofsPos)
} nsb_ck_clip_t;

int DecodeNSBCK_Clip (nsb_ck_clip_t *clip, const uint8_t *data, size_t size, uint32_t clip_idx);

// Sample a decoded clip's node transform at `frame`: 3x3 basis matrix `m`
// (row-major, identity by default) and translation `pos` (zero by default).
// Returns 1 on success, 0 when node/frame is out of range.
int NSBCK_SampleMatrix (const nsb_ck_clip_t *clip, uint32_t node, uint32_t frame,
	float m[9], float pos[3]);

uint8_t *BuildNSBCK (uint32_t num_frame, const nsb_ck_clip_spec_t *clips,
	size_t num_clips, size_t *out_size);

// Encode model animations back to NSB* binary.
// Returns a malloc'd buffer (free by caller), sets *out_size. NULL on error.
uint8_t *EncodeNSBCA (const model_t *model, size_t *out_size);
uint8_t *EncodeNSBTA (const model_t *model, size_t *out_size);
uint8_t *EncodeNSBTP (const model_t *model, size_t *out_size);
uint8_t *EncodeNSBVA (const model_t *model, size_t *out_size);
uint8_t *EncodeNSBMA (const model_t *model, size_t *out_size);
uint8_t *EncodeNSBCK (const model_t *model, size_t *out_size);

// Scan a directory for NSB* animation files that are siblings of the given
// NSBMD path and merge them into the model.  The directory layout expected
// is the flat NDS style: <dir>/<name>.nsbmd alongside <dir>/<name>.nsbca etc.
void ImportNSBAnimSiblings (model_t *model, const char *nsbmd_path);

#endif
