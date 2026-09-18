// SPDX-License-Identifier: GPL-2.0+
// Paper Mario (Switch) lighting data: probe headers and render params.
//
// Native port of KillzXGaming/Paper-Mario-Tools' LightConverter
// (ProbeHeader.cs, RenderSettings/RenderParams.cs, RenderSettings/Hashing.cs,
// RenderSettings/crc32.cs) for Paper Mario: The Origami King / The Thousand-
// Year Door style "probe.header" binaries and ".data" render-param texts.
// Scene/light animation BFRES payloads ("light.bfres(.zst)") need no new
// code: Zstandard (FF_ZSTD) + BFRES/FSCN (ParseBFRESAnims) already cover
// that container path; only the two sidecar formats below were missing.

#ifndef SZS_LIB_PAPERMARIO_H
#define SZS_LIB_PAPERMARIO_H 1

#include "types.h"
#include <stdio.h>

//-----------------------------------------------------------------------------
// probe.header: fixed-size little-endian binary, total 156 + 4*N bytes
// (N = axis texture count, 4 on retail files):
//
//	u32 magic/version (100), u32 N, u32 axis[N],
//	float pos[3], float box_scale[3], float unk[4],
//	float param1, float param2, float color[3], float unk2[3],
//	char type[64] (UTF-8, NUL padded),
//	u32 unkA0, float unkA4, float unkA8, u32 unkAC
//-----------------------------------------------------------------------------

#define PMPROBE_MAGIC 100
#define PMPROBE_MAX_AXIS 64

typedef struct pmprobe_t
{
	u32 *axis; // N axis parameters (malloc'd, NULL if N==0)
	u32 n_axis;
	float pos[3];
	float scale[3];
	float unk[4];
	float param1;
	float param2;
	float color[3];
	float unk2[3];
	char type[65]; // NUL terminated, max 64 UTF-8 bytes on encode
	u32 unkA0;
	float unkA4;
	float unkA8;
	u32 unkAC;
} pmprobe_t;

bool IsPMProbe (const u8 *data, size_t size);

void InitializePMProbe (pmprobe_t *probe);
void ResetPMProbe (pmprobe_t *probe);

// Strict parse: size must fit 156 + 4*N exactly, magic must be 100.
enumError ScanPMProbe (pmprobe_t *probe, const u8 *data, size_t size);

// Canonical little-endian encode (byte-exact inverse of ScanPMProbe).
enumError EncodePMProbe (const pmprobe_t *probe, u8 **dest, size_t *dest_size);

// JSON text (upstream ProbeHeader JSON shape) -> struct and back.
enumError DecodePMProbe_JSON (FILE *out, const u8 *data, size_t size);
enumError ParsePMProbe_JSON (pmprobe_t *probe, const char *text, size_t text_len);

// source is a .json file, dest receives the binary probe.header.
enumError encode_pmprobe_file (ccp source, ccp dest);

//-----------------------------------------------------------------------------
// render params ".data": UTF-8 BOM + LF text of 8-digit hex hashes:
//
//	{nnnnnnnn}\n				section count
//	{hash}:{size}\n			per section (size = body bytes)
//	{hash}\n				start of each section body
//	{phash}:{pvalue}\n			per property
//
// Scalar float values are 8-digit hex bit patterns ("0x3f800000", "0x0" for
// zero); 2/3/4-float vectors are comma separated "%f" decimals; integers are
// decimal; anything else is a raw string. Hashes resolve to names through
// the upstream hash_strings.txt table (first-hash-wins); unknown hashes use
// unpadded uppercase hex, exactly like the reference tool.
//-----------------------------------------------------------------------------

typedef enum pmrender_kind_t
{
	PMR_INT = 0, // signed decimal integer
	PMR_FLOAT = 1, // float scalar (hex in .data, number in JSON)
	PMR_VEC2 = 2, // 2 floats
	PMR_VEC3 = 3, // 3 floats
	PMR_VEC4 = 4, // 4 floats
	PMR_STRING = 5, // raw string
} pmrender_kind_t;

typedef struct pmrender_val_t
{
	pmrender_kind_t kind;
	int64_t ival; // PMR_INT
	float fval; // PMR_FLOAT
	float vec[4]; // PMR_VEC2/3/4
	char *sval; // PMR_STRING (malloc'd)
} pmrender_val_t;

typedef struct pmrender_prop_t
{
	char *key; // resolved name or "X"-format hex (malloc'd)
	pmrender_val_t val;
} pmrender_prop_t;

typedef struct pmrender_section_t
{
	u32 hash;
	char *name; // resolved name or "X"-format hex (malloc'd)
	pmrender_prop_t *props; // malloc'd, file order
	size_t n_props;
} pmrender_section_t;

typedef struct pmrender_t
{
	pmrender_section_t *sections; // malloc'd, file order
	size_t n_sections;
} pmrender_t;

bool IsPMRender (const u8 *data, size_t size);

void InitializePMRender (pmrender_t *render);
void ResetPMRender (pmrender_t *render);

// Strict parse of the .data text (BOM required, exact size fit).
enumError ScanPMRender (pmrender_t *render, const u8 *data, size_t size);

// Canonical .data encode (byte-exact inverse of ScanPMRender).
enumError EncodePMRender (const pmrender_t *render, u8 **dest, size_t *dest_size);

// JSON text (upstream RenderParams JSON shape) -> struct and back.
enumError DecodePMRender_JSON (FILE *out, const u8 *data, size_t size);
enumError ParsePMRender_JSON (pmrender_t *render, const char *text, size_t text_len);

// source is a .json file, dest receives the binary .data text.
enumError encode_pmrender_file (ccp source, ccp dest);

// CRC32 (IEEE, zlib polynomial) used for name -> hash; exposed for tests.
u32 PMRender_Hash (const char *name, size_t len);

// Resolve a hash to its upstream string, or NULL if unknown.
ccp PMRender_Lookup (u32 hash);

#endif // SZS_LIB_PAPERMARIO_H
