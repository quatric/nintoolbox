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

// CHR0: Nintendo NW4R "bone animation" BRRES sub-file. Added by this fork.
//
// CHR0 animates a model's skeleton. Each entry is named after a bone of a
// sibling MDL0 and carries up to nine animated scalar channels -- scale XYZ,
// rotation XYZ and translation XYZ. Every channel is independently either
// "fixed" (a single float, constant for the whole animation) or a track of
// keyframes in one of the six shared encodings implemented by lib-brres-anim.
//
// A 32-bit "code" word per entry selects, per channel group, whether the
// group is present at all, whether it is isotropic (one shared value for all
// three axes), which of the three axes are fixed, and which track encoding
// the group's tracks use.
//
// Binary layout from BrawlLib (BrawlLib/SSBB/Types/Animations/CHR0.cs,
// ResourceNodes/Animations/CHR0Node.cs, Wii/Models/AnimationCode.cs,
// Wii/Animations/AnimationConverter.cs), corrected against retail Mario Kart
// Wii data where BrawlLib's layout did not hold -- see the notes on the
// version dependent format-field position in lib-chr.c.

#ifndef SZS_LIB_CHR_H
#define SZS_LIB_CHR_H 1

#include "lib-std.h"
#include "lib-brres-anim.h"

///////////////////////////////////////////////////////////////////////////////

#define CHR0_MIN_VERSION 3
#define CHR0_MAX_VERSION 5
#define CHR0_DEFAULT_VERSION 4

// nine channels: scale XYZ, rotation XYZ, translation XYZ
#define CHR0_N_CHANNEL 9

// three channel groups: scale, rotation, translation
#define CHR0_N_GROUP 3

///////////////////////////////////////////////////////////////////////////////
// [[chr0_channel_t]]

typedef struct chr0_channel_t
{
	bool is_fixed; // true: 'value' is constant, 'track' unused
	float value; // only valid if 'is_fixed'
	banim_track_t track; // only valid if !'is_fixed'

} chr0_channel_t;

///////////////////////////////////////////////////////////////////////////////
// [[chr0_group_t]]

typedef struct chr0_group_t
{
	bool exists; // false: group absent, no data at all
	bool isotropic; // true: only channel [0] is stored, shared by XYZ
	banim_format_t format; // track encoding used by this group

} chr0_group_t;

///////////////////////////////////////////////////////////////////////////////
// [[chr0_entry_t]]

typedef struct chr0_entry_t
{
	ccp name; // alloced bone name, matches a sibling MDL0 bone
	u32 code; // raw code word, preserved verbatim for re-encoding

	chr0_group_t group[CHR0_N_GROUP];
	chr0_channel_t channel[CHR0_N_CHANNEL];

} chr0_entry_t;

///////////////////////////////////////////////////////////////////////////////
// [[chr0_t]]

typedef struct chr0_t
{
	ccp fname; // alloced filename of loaded file
	FileAttrib_t fatt; // file attribute

	uint version; // 3..5
	ccp name; // alloced resource name of the animation, or NULL
	ccp orig_path; // alloced original source path, or NULL
	uint n_frames; // number of frames
	bool loop; // true: animation loops
	u32 scaling_rule; // NW4R scaling rule, preserved verbatim

	chr0_entry_t *entry; // list of entries, alloced
	uint n_entry; // used elements of 'entry'
	uint n_entry_alloced; // alloced elements of 'entry'

} chr0_t;

///////////////////////////////////////////////////////////////////////////////

void InitializeCHR0 (chr0_t *chr);
void ResetCHR0 (chr0_t *chr);

chr0_entry_t *AppendEntryCHR0 (chr0_t *chr, ccp name);

// frame count including the extra loop frame; linear track formats need it
uint GetFrameLimitCHR0 (const chr0_t *chr);

//-----------------------------------------------------------------------------

enumError ScanRawCHR0 (chr0_t *chr, // CHR0 data structure
	bool init_chr, // true: initialize 'chr' first
	const void *data, // data to scan
	uint data_size // size of 'data'
);

//-----------------------------------------------------------------------------

enumError ScanTextCHR0 (chr0_t *chr, // CHR0 data structure
	bool init_chr, // true: initialize 'chr' first
	ccp src_fname // name of the text source to load
);

///////////////////////////////////////////////////////////////////////////////

enumError SaveRawCHR0 (chr0_t *chr, // pointer to valid CHR0
	ccp fname, // filename of destination
	bool set_time // true: set time stamps
);

//-----------------------------------------------------------------------------

enumError SaveTextCHR0 (chr0_t *chr, // pointer to valid CHR0
	ccp fname, // filename of destination
	bool set_time // true: set time stamps
);

///////////////////////////////////////////////////////////////////////////////

#endif // SZS_LIB_CHR_H
