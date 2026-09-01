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

// SRT0: Nintendo NW4R "texture SRT animation" BRRES sub-file. Added by this
// fork.
//
// SRT0 animates the texture matrix of a material: scale X/Y, rotation, and
// translation X/Y, per texture layer. Each entry is named after a material of
// a sibling MDL0 and holds a bitmask of which of the material's 8 texture
// layers and 3 indirect layers are animated; each set bit contributes one
// "texture entry" of five channels.
//
// Unlike CHR0, SRT0 uses exactly one track encoding -- I12, raw float
// keyframes -- for every animated channel, so it has no format-selector
// ambiguity. Layout from BrawlLib
// (BrawlLib/SSBB/Types/Animations/SRT0.cs, ResourceNodes/Animations/
// SRT0Node.cs, Wii/Animations/AnimationConverter.cs DecodeSRT0Keyframes).

#ifndef SZS_LIB_SRT_H
#define SZS_LIB_SRT_H 1

#include "lib-std.h"
#include "lib-brres-anim.h"

///////////////////////////////////////////////////////////////////////////////

#define SRT0_MIN_VERSION 4
#define SRT0_MAX_VERSION 5
#define SRT0_DEFAULT_VERSION 4

// scale X, scale Y, rotation, translation X, translation Y
#define SRT0_N_CHANNEL 5

// 8 ordinary texture layers + 3 indirect layers
#define SRT0_N_TEXTURE 8
#define SRT0_N_INDIRECT 3

///////////////////////////////////////////////////////////////////////////////
// [[srt0_channel_t]]

typedef struct srt0_channel_t
{
	bool is_fixed; // true: 'value' is constant, 'track' unused
	float value; // only valid if 'is_fixed'
	banim_track_t track; // only valid if !'is_fixed'

} srt0_channel_t;

///////////////////////////////////////////////////////////////////////////////
// [[srt0_texture_t]]
//
// One animated texture layer of a material.

typedef struct srt0_texture_t
{
	bool indirect; // true: an indirect layer, false: an ordinary one
	uint layer; // layer index within its kind
	u32 code; // raw code word, preserved verbatim

	srt0_channel_t channel[SRT0_N_CHANNEL];

} srt0_texture_t;

///////////////////////////////////////////////////////////////////////////////
// [[srt0_entry_t]]

typedef struct srt0_entry_t
{
	ccp name; // alloced material name, matches a sibling MDL0 material
	u32 tex_mask; // bitmask of animated ordinary texture layers
	u32 ind_mask; // bitmask of animated indirect layers

	srt0_texture_t *texture; // alloced list of animated layers
	uint n_texture; // used elements of 'texture'

} srt0_entry_t;

///////////////////////////////////////////////////////////////////////////////
// [[srt0_t]]

typedef struct srt0_t
{
	ccp fname; // alloced filename of loaded file
	FileAttrib_t fatt; // file attribute

	uint version; // 4 or 5
	ccp name; // alloced resource name of the animation, or NULL
	ccp orig_path; // alloced original source path, or NULL
	uint n_frames; // number of frames
	bool loop; // true: animation loops
	u32 matrix_mode; // texture matrix mode, preserved verbatim

	srt0_entry_t *entry; // list of entries, alloced
	uint n_entry; // used elements of 'entry'
	uint n_entry_alloced; // alloced elements of 'entry'

} srt0_t;

///////////////////////////////////////////////////////////////////////////////

void InitializeSRT0 (srt0_t *srt);
void ResetSRT0 (srt0_t *srt);

srt0_entry_t *AppendEntrySRT0 (srt0_t *srt, ccp name);
srt0_texture_t *AppendTextureSRT0 (srt0_entry_t *entry, bool indirect, uint layer);

// frame count including the extra loop frame
uint GetFrameLimitSRT0 (const srt0_t *srt);

//-----------------------------------------------------------------------------

enumError ScanRawSRT0 (srt0_t *srt, // SRT0 data structure
	bool init_srt, // true: initialize 'srt' first
	const void *data, // data to scan
	uint data_size // size of 'data'
);

//-----------------------------------------------------------------------------

enumError ScanTextSRT0 (srt0_t *srt, // SRT0 data structure
	bool init_srt, // true: initialize 'srt' first
	ccp src_fname // name of the text source to load
);

///////////////////////////////////////////////////////////////////////////////

enumError SaveRawSRT0 (srt0_t *srt, // pointer to valid SRT0
	ccp fname, // filename of destination
	bool set_time // true: set time stamps
);

//-----------------------------------------------------------------------------

enumError SaveTextSRT0 (srt0_t *srt, // pointer to valid SRT0
	ccp fname, // filename of destination
	bool set_time // true: set time stamps
);

///////////////////////////////////////////////////////////////////////////////

#endif // SZS_LIB_SRT_H
