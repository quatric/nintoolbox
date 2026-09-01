/***************************************************************************
 *                         _______ _______ _______                         *
 *                        |  ___  |____   |  ___  |                        *
 *                        | |   |_|    / /| |   |_|                        *
 *                        | |_____    / / | |_____                         *
 *                        |_____  |  / /  |_____  |                        *
 *                         _    | | / /    _    | |                        *
 *                        | |___| |/ /____| |___| |                        *
 *                        |_______|_______|_______|                        *
 *                                                                         *
 *                            Wiimms SZS Tools                             *
 *                          https://szs.wiimm.de/                          *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This file is part of the SZS project.                                 *
 *   Visit https://szs.wiimm.de/ for project details and sources.          *
 *                                                                         *
 ***************************************************************************/

// Shared keyframe/track codec for the NW4R BRRES animation family
// (CHR0, SRT0, CLR0, SHP0). Added by this fork.
//
// CHR0 and SRT0 encode each animated scalar channel ("track") with one of
// six interchangeable encodings, selected per channel group by a bitfield in
// the entry's code word. Three are *indexed* (sparse keyframes carrying an
// explicit frame index and a Hermite tangent) and three are *linear* (one
// dense value per frame, tangent implied):
//
//   I4  : 16-byte header + 4 bytes/key   quantized, index 8b / step 12b / tangent 12b
//   I6  : 16-byte header + 6 bytes/key   quantized, index 11b / step u16 / tangent s16
//   I12 : 8-byte header  + 12 bytes/key  three raw big-endian floats
//   L1  : 8-byte header  + 1 byte/frame  quantized u8
//   L2  : 8-byte header  + 2 bytes/frame quantized u16
//   L4  : no header      + 4 bytes/frame raw big-endian float
//
// Quantized formats reconstruct a value as  base + raw * step,  with 'base'
// and 'step' carried in the track header. This implementation deliberately
// preserves each track's original format AND its header parameters through
// decode -> text -> encode, and re-derives the integer 'raw' fields by
// inverting that same expression. Because base/step survive verbatim, the
// inversion reproduces the original integers exactly, so a decode/encode
// round trip of an unedited track is byte identical rather than merely
// semantically equal.
//
// Layout reverse-engineered from BrawlLib
// (BrawlLib/Wii/Animations/EncodingTypes.cs for the six headers/entries,
// BrawlLib/Wii/Animations/AnimationConverter.cs DecodeFrames() for the
// reconstruction arithmetic, BrawlLib/Wii/Models/AnimationCode.cs for the
// CHR0 code word).

#ifndef SZS_LIB_BRRES_ANIM_H
#define SZS_LIB_BRRES_ANIM_H 1

#include "lib-std.h"

///////////////////////////////////////////////////////////////////////////////
// [[banim_format_t]]

typedef enum banim_format_t
{
	BANIM_NONE = 0,
	BANIM_I4 = 1,
	BANIM_I6 = 2,
	BANIM_I12 = 3,
	BANIM_L1 = 4,
	BANIM_L2 = 5,
	BANIM_L4 = 6,

} banim_format_t;

// name of a format, e.g. "I12"; returns "?" for invalid values
ccp GetFormatNameBANIM (banim_format_t fmt);

// inverse of GetFormatNameBANIM(); returns BANIM_NONE if unknown
banim_format_t ScanFormatNameBANIM (ccp name);

// true: 'fmt' stores explicit per-key frame indices (I4/I6/I12)
bool IsIndexedBANIM (banim_format_t fmt);

///////////////////////////////////////////////////////////////////////////////
// [[banim_key_t]]

typedef struct banim_key_t
{
	float frame; // frame index of this key
	float value; // animated value at 'frame'
	float tangent; // Hermite tangent (slope) at 'frame'

} banim_key_t;

///////////////////////////////////////////////////////////////////////////////
// [[banim_track_t]]
//
// One animated scalar channel. A channel that is not animated at all is
// represented by the owning format's own "fixed" flag plus a single float,
// not by a track, so every track here has at least one key.

typedef struct banim_track_t
{
	banim_format_t format; // encoding of the source data

	// header parameters, preserved verbatim so that re-encoding a
	// quantized track reproduces the original integers bit for bit
	float frame_scale; // I4/I6/I12 header field
	float step; // I4/I6/L1/L2 quantization step
	float base; // I4/I6/L1/L2 quantization base
	u16 unknown; // I4/I6 header padding/unknown field

	banim_key_t *key; // alloced list of keys
	uint n_key; // used elements of 'key'
	uint n_key_alloced; // alloced elements of 'key'

} banim_track_t;

///////////////////////////////////////////////////////////////////////////////

void InitializeTrackBANIM (banim_track_t *tr);
void ResetTrackBANIM (banim_track_t *tr);

banim_key_t *AppendKeyBANIM (banim_track_t *tr, float frame, float value, float tangent);

///////////////////////////////////////////////////////////////////////////////

// Decode one track. 'data' points at the track header, 'avail' is the number
// of readable bytes from there to the end of the file. 'frame_limit' is the
// animation's frame count including the extra loop frame; it is only needed
// by the linear formats, which carry no key count of their own.

enumError DecodeTrackBANIM (banim_track_t *tr, // destination, initialized here
	const u8 *data, // start of the track header
	uint avail, // readable bytes from 'data' on
	banim_format_t format, // encoding to decode
	uint frame_limit // frame count incl. loop frame (linear formats)
);

//-----------------------------------------------------------------------------

// Size in bytes that EncodeTrackBANIM() will write for 'tr'.
uint GetEncodedSizeBANIM (const banim_track_t *tr, uint frame_limit);

// Encode 'tr' into 'dest', which must have room for GetEncodedSizeBANIM()
// bytes. Returns the number of bytes written.
uint EncodeTrackBANIM (const banim_track_t *tr, u8 *dest, uint frame_limit);

//
///////////////////////////////////////////////////////////////////////////////
///////////////		   BRRES trailing string pool		///////////////
///////////////////////////////////////////////////////////////////////////////
//
// A BRRES sub-file keeps its names in a pool that follows the data section and
// is NOT covered by the sub-file's own size field -- inside a container the
// pool is shared with the rest of the BRRES. Each name is stored as a u32
// big endian length followed by the NUL terminated characters, and every
// length word starts on a 4 byte boundary. All string offsets point at the
// first character, not at the length word.
//
// Both functions take the same 'names' list and 'pool_start' (the file offset
// where the pool begins, which must be 4 byte aligned) so that a size pass and
// a write pass agree exactly. Empty or NULL names are skipped and get an
// offset of 0.

// Compute the pool size and, if 'char_off' is not NULL, the absolute file
// offset of each name's first character.
uint CalcPoolBANIM (ccp *names, uint n_names, uint pool_start, uint *char_off);

// Write the pool into 'file_base' at 'pool_start'. The buffer must already
// hold pool_start + CalcPoolBANIM(...) bytes.
void WritePoolBANIM (u8 *file_base, ccp *names, uint n_names, uint pool_start);

// Same as the two above, but laying the pool out in ordinal name order, which
// is what retail does. CalcPoolSortedBANIM() returns the offsets scattered
// back onto the caller's logical slots, so callers need no other change.
uint CalcPoolSortedBANIM (ccp *names, uint n_names, uint pool_start, uint *char_off);
void WritePoolSortedBANIM (u8 *file_base, ccp *names, uint n_names, uint pool_start);

///////////////////////////////////////////////////////////////////////////////

#endif // SZS_LIB_BRRES_ANIM_H
