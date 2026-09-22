// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Blue Tongue "Toshi" engine TSFB/TRB containers (Nicktoons, Nickelodeon
// Barnyard Wii). Layout after OpenBarnyard (TTRB.h, TCompress_Decompress.cpp).
//
// TSFB: "TSFB", u32 file size - 8, "FBRT", then big-endian hunks of
//   {char tag[4], u32 size} padded to 4 bytes: "XRDH" header, "TCES" section
//   (or "CCES", BTEC compressed), "CLER" relocations {u32 count, {u32 section,
//   u32 offset} pointer fix-ups; pointers are stored as section offsets}, and
//   "BMYS" symbols {u32 count, count * {u16 section, u16 name offset, u32
//   hash, u32 data offset}, NUL-terminated names}.
// BTEC: "CETB", u16 major, u16 minor (1.2/1.3), u32 compressed size, u32
//   size, [u32 xor for 1.3], then commands: byte {bit 7 no-offset, bit 6
//   14-bit size, bits 0..5 size-1}, optional second size byte, unless
//   no-offset a 7/15-bit back offset - 1 (bit 7 = second byte), and for
//   no-offset commands SIZE literal bytes.
//
// .ttl texture library (symbol "TTL"): {u32 count, u32 ptr to entries, u32
//   ptr pack name}, entries of 13 words: format (0x300 + I4, IA4, I8, IA8,
//   RGB565, RGB5A3, RGBA8, CMPR, Z8, Z16, Z24X8, CI4_RGB565, CI4_RGB5A3,
//   CI8_RGB565, CI8_RGB5A3, CI8_IA8 from 1), name ptr, width, height, 0,
//   data ptr, base level size, palette ptr, palette ptr, palette entries,
//   TLUT format (GX), extra mip levels, total data size (all levels).
//
// .tkl keyframe library (symbol "keylib"): TKeyframeLibrary::TRBHeader, 52
//   bytes (confirmed against InfiniteC0re/OpenBarnyard, Toshi/Source/Render/
//   TAnimation.h, and cross-checked byte-for-byte on ~250 disc samples):
//   u32 name ptr (section offset, NUL-terminated string), TVector3 (3 BE
//   floats) translation quantization scale, s32 numTranslations,
//   s32 numQuaternions, s32 numScales, s32 translationSize (bytes/entry,
//   always 6 = packed s16 x,y,z), s32 quaternionSize (always 8 = packed
//   s16 x,y,z,w), s32 scaleSize (always 4 = plain BE float; no sample file
//   has numScales != 0, so this element type is unverified by real data),
//   u32 translations ptr, u32 quaternions ptr, u32 scales ptr (all section
//   offsets). Translation = packed s16 * scale (per-axis, but scale.x ==
//   scale.y == scale.z in every observed file). Quaternion = packed s16 /
//   32767.0 (x,y,z,w); verified: decoded magnitude is 1.0 +/- 2e-5 on real
//   data. This is animation curve data (keyframe pools), not vertex/model
//   geometry -- despite the "model" framing in some tooling, TKL has no
//   bones, batches or indices; skeleton binding lives elsewhere (TSkeleton*
//   symbols, not decoded here).
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_TOSHI_H
#define SZS_LIB_TOSHI_H 1

#include "lib-std.h"

typedef struct
{
	u8 *sect; // decompressed section, owned
	size_t sect_size;
	const u8 *symb; // points into the file
	size_t symb_size;
} toshi_trb_t;

bool IsToshiTsfb (const u8 *data, size_t size);
enumError OpenToshiTrb (toshi_trb_t *trb, const u8 *data, size_t size);
void ResetToshiTrb (toshi_trb_t *trb);
// Section offset of a named symbol, or -1.
s64 ToshiSymbol (const toshi_trb_t *trb, ccp name);

uint ToshiTtlCount (const toshi_trb_t *trb);
// Name (points into the section) and decoded first level of texture IDX.
enumError DecodeToshiTexture (const toshi_trb_t *trb, uint idx, ccp *name, u8 **rgba, uint *width, uint *height);

//-----------------------------------------------------------------------------
// .tkl keyframe library (symbol "keylib"): a pool of animation keyframes
// shared between a model's skeleton (SkeletonHeader.m_szTKLName) and its
// TSkeletonSequence tracks (bone-index base offsets into these pools, not
// stored here). Layout after Toshi::TKeyframeLibrary::TRBHeader (TAnimation.h):
// {char *name, TVector3 scale, s32 numTranslations, s32 numQuaternions,
// s32 numScales, s32 translationSize, s32 quaternionSize, s32 scaleSize,
// TVector3 *translations, TQuaternion *quaternions, float *scales}, all
// pointers stored as section offsets (confirmed against the CLER relocation
// list, which lists exactly these 4 pointer fields).
//
// VERIFIED (checked byte-for-byte, cross-referenced across 250 disc samples):
//   translations: numTranslations * 6 bytes, {s16 x,y,z} * scale (per axis,
//     scale.x==scale.y==scale.z in every sample seen).
//   quaternions: numQuaternions * 8 bytes, {s16 x,y,z,w} / 32767.0 -- a unit
//     quaternion in every sample checked (|q650| == 1.000 +/- 2e-5).
// BEST-EFFORT (numScales==0 in every disc sample found so far, so this path
// is unverified): scales: numScales * scaleSize bytes; decoded as a plain
// float when scaleSize==4, else as s16/32767.0 by analogy with quaternions.
//-----------------------------------------------------------------------------
typedef struct
{
	ccp name;
	float scale[3];
	u32 num_t, num_q, num_s;
	u32 tsize, qsize, ssize; // bytes per entry
	const u8 *t_data, *q_data, *s_data; // raw, still needs per-entry decode
} toshi_tkl_t;

// Finds the "keylib" symbol and fills *tkl with pointers into trb->sect.
enumError OpenToshiTkl (const toshi_trb_t *trb, toshi_tkl_t *tkl);
// Decodes translation IDX to a float[3] (already scaled). VERIFIED format.
bool ToshiTklTranslation (const toshi_tkl_t *tkl, uint idx, float out[3]);
// Decodes quaternion IDX to a float[4] {x,y,z,w}. VERIFIED format.
bool ToshiTklQuaternion (const toshi_tkl_t *tkl, uint idx, float out[4]);
// Decodes scale IDX to a float. BEST-EFFORT format (see toshi_tkl_t comment).
bool ToshiTklScale (const toshi_tkl_t *tkl, uint idx, float *out);

#endif
