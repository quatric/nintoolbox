// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Luigi's Mansion (GameCube) data tables: JMP parameter tables (.jmp),
// KEY skeletal animations (.key), TMB fade-effect timing (.tmb), GEB sprite
// data (.geb) and SLS morph data (.sls).
//
// Re-implemented in C from KillzXGaming/MdlConverter's GCNLibrary/LM
// (MIT-licensed, tool by KillzXGaming; research by opeyx and SpaceCats).
// Layout recovered from JMP_Parser (+JMPHashHelper), KEY_Parser,
// TMB_Parser, GEB_Parser and SLS_Parser and re-expressed in this project's
// C style. No upstream code is copied. (Upstream BAS_Parser is an empty
// stub with no format to port; SLK_Parser is likewise unimplemented
// upstream, so .slk files are only identified, not converted.)
//
// All five formats are big-endian with full structural validation; every
// decoder has a byte-exact encoder plus a line-based text dump/load pair
// for inspection. JMP field-name hashes resolve through the format's own
// known-name table where possible, falling back to hex.
//-----------------------------------------------------------------------------
#ifndef LIB_LMDATA_H
#define LIB_LMDATA_H 1

#include "types.h"

//---------------- JMP ----------------
// Header {u32 records, u32 fields, u32 recOff, u32 recSize} (either byte
// order; detected structurally), then field descriptors {u32 hash,
// u32 bitmask, u16 offset, s8 shift, u8 type} and fixed-size records.
// Types: 0 int32, 1 string, 2 float, 4 int16, 5 byte, 6 Shift-JIS string.

typedef struct
{
	char name[64]; // hash-resolved name or "hash_%08X"
	uint hash, bitmask;
	uint offset;
	int shift; // applied to integer types
	uint type;
} lmjmp_field_t;

typedef struct
{
	char **values; // one NUL-terminated string per field (numbers rendered)
	u8 *raw; // original record bytes (for masked-bit-preserving rebuilds)
	uint raw_size;
} lmjmp_record_t;

typedef struct
{
	bool big_endian;
	lmjmp_field_t *fields;
	uint n_fields;
	lmjmp_record_t *records;
	uint n_records;
	uint rec_size;
} lmjmp_t;

bool IsLMJMP (const u8 *data, size_t size);
enumError ScanLMJMP (lmjmp_t *jmp, const u8 *data, uint size);
void ResetLMJMP (lmjmp_t *jmp);
enumError CreateLMJMP (u8 **dest, uint *dest_size, const lmjmp_t *jmp);
// Text dump/load (one "field = value" line per record field, "#"-comments).
enumError DumpLMJMP (char **dest, const lmjmp_t *jmp);
enumError ParseLMJMPText (lmjmp_t *jmp, const char *text);

//---------------- KEY ----------------
// Header {u32 joints, u16 frames, u16 delay, u32 flags, 5x u32 offsets}
// then per-joint begin indices (9x u32), key counts (9x u8-pair) and
// keyframe streams for scale/rotation/translation XYZ.

typedef struct
{
	float frame, value, slope;
} lmkey_frame_t;

typedef struct
{
	lmkey_frame_t *keys;
	uint n_keys;
	uint begin;
} lmkey_group_t;

typedef struct
{
	lmkey_group_t sx, sy, sz, rx, ry, rz, px, py, pz;
} lmkey_joint_t;

typedef struct
{
	lmkey_joint_t *joints;
	uint n_joints;
	uint frames, delay, flags;
} lmkey_t;

bool IsLMKEY (const u8 *data, size_t size);
enumError ScanLMKEY (lmkey_t *key, const u8 *data, uint size);
void ResetLMKEY (lmkey_t *key);
enumError CreateLMKEY (u8 **dest, uint *dest_size, const lmkey_t *key);
enumError DumpLMKEY (char **dest, const lmkey_t *key);
enumError ParseLMKEYText (lmkey_t *key, const char *text);

//---------------- TMB ----------------
// {u16 sequences, u16 duration, u32 seqOff} then sequences {char[28] name,
// u32 keyframes, u16 begin, u16 elements} with float key data at absolute
// 8 + begin*4.

typedef struct
{
	char name[32];
	float *frames; // keyframe times
	float *values; // (elements-1) values per keyframe, row-major
	uint n_keys, elements;
} lmtmb_seq_t;

typedef struct
{
	lmtmb_seq_t *seqs;
	uint n_seqs;
	uint duration;
} lmtmb_t;

bool IsLMTMB (const u8 *data, size_t size);
enumError ScanLMTMB (lmtmb_t *tmb, const u8 *data, uint size);
void ResetLMTMB (lmtmb_t *tmb);
enumError CreateLMTMB (u8 **dest, uint *dest_size, const lmtmb_t *tmb);
enumError DumpLMTMB (char **dest, const lmtmb_t *tmb);
enumError ParseLMTMBText (lmtmb_t *tmb, const char *text);

//---------------- GEB ----------------
// {u32 count} then sprites {s16 fadeBone, s16 glowBone, RGBA, 4x (vec2 +
// u32 zero), 4x vec2 texcoords, f32 intensity, vec2 relPos} (68 bytes each).

typedef struct
{
	int fade_bone, glow_bone;
	u8 r, g, b, a;
	float pts[8], uvs[8], intensity, rel[2];
} lmgeb_sprite_t;

typedef struct
{
	lmgeb_sprite_t *sprites;
	uint n_sprites;
} lmgeb_t;

bool IsLMGEB (const u8 *data, size_t size);
enumError ScanLMGEB (lmgeb_t *geb, const u8 *data, uint size);
void ResetLMGEB (lmgeb_t *geb);
enumError CreateLMGEB (u8 **dest, uint *dest_size, const lmgeb_t *geb);
enumError DumpLMGEB (char **dest, const lmgeb_t *geb);
enumError ParseLMGEBText (lmgeb_t *geb, const char *text);

//---------------- SLS ----------------
// Morph positions: {u32 pad, u32 pad2, u16 shapes, u16 groups, u32 hash,
// 8x u32 offsets} then shape/group infos and float position pools.

typedef struct
{
	u16 pos_start, pos_count, nrm_start, nrm_count;
} lmsls_group_t;

typedef struct
{
	float (*positions)[3];
	uint n_positions;
	lmsls_group_t *groups;
	uint n_groups;
} lmsls_t;

bool IsLMSLS (const u8 *data, size_t size);
enumError ScanLMSLS (lmsls_t *sls, const u8 *data, uint size);
void ResetLMSLS (lmsls_t *sls);
enumError CreateLMSLS (u8 **dest, uint *dest_size, const lmsls_t *sls);
enumError DumpLMSLS (char **dest, const lmsls_t *sls);
enumError ParseLMSLSText (lmsls_t *sls, const char *text);

#endif // LIB_LMDATA_H
