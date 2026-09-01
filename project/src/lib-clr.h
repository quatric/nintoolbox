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

// CLR0: Nintendo NW4R "material color animation" BRRES sub-file.
// Added by this fork.
//
// CLR0 animates up to 11 GX color targets per named material: the two light
// channel material colors, the two light channel ambient colors, the three
// TEV color registers and the four TEV konstant registers. Each target is
// either absent, a single constant RGBA, or one RGBA per frame.
//
// Layout reverse-engineered from BrawlLib
// (BrawlLib/SSBB/Types/Animations/CLR0.cs: CLR0v3/CLR0v4, CLR0Material,
// CLR0MaterialEntry, CLR0EntryFlags, EntryTarget).

#ifndef SZS_LIB_CLR_H
#define SZS_LIB_CLR_H 1

#include "lib-std.h"

///////////////////////////////////////////////////////////////////////////////

#define CLR0_MIN_VERSION 3
#define CLR0_MAX_VERSION 4
#define CLR0_DEFAULT_VERSION 4

// number of animatable color targets, in binary flag order
#define CLR0_N_TARGET 11

// per-target flag bits within the material flags word: target 't' uses
// bit (2*t) for "exists" and bit (2*t+1) for "constant"
#define CLR0_BIT_EXISTS(t) (1u << (2 * (t)))
#define CLR0_BIT_CONSTANT(t) (1u << (2 * (t) + 1))

// short name of target 't' (e.g. "tevreg0"); NULL if out of range
ccp GetTargetNameCLR0 (uint target);

// inverse of GetTargetNameCLR0(); returns -1 if unknown
int ScanTargetNameCLR0 (ccp name);

///////////////////////////////////////////////////////////////////////////////
// [[clr0_target_t]]

typedef struct clr0_target_t
{
	bool exists; // false: this target is not animated at all
	bool is_constant; // true: 'color' holds the single value, 'color_list' unused
	u32 mask; // RGBA color mask applied to the source color
	u32 color; // only relevant if 'is_constant'
	u32 *color_list; // else n_frames+1 RGBA values, alloced
	uint n_color; // number of used elements of 'color_list'

} clr0_target_t;

///////////////////////////////////////////////////////////////////////////////
// [[clr0_entry_t]]

typedef struct clr0_entry_t
{
	ccp name; // alloced material name
	u32 flags; // raw flags word, preserved verbatim for exact re-encode
	clr0_target_t target[CLR0_N_TARGET];

} clr0_entry_t;

///////////////////////////////////////////////////////////////////////////////
// [[clr0_t]]

typedef struct clr0_t
{
	ccp fname; // alloced filename of loaded file
	FileAttrib_t fatt; // file attribute

	uint version; // 3 or 4
	ccp name; // alloced resource name of the animation, or NULL
	ccp orig_path; // alloced original source path, or NULL
	uint n_frames; // number of frames
	bool loop; // true: animation loops

	clr0_entry_t *entry; // list of entries, alloced
	uint n_entry; // number of used elements of 'entry'
	uint n_entry_alloced; // number of alloced elements of 'entry'

} clr0_t;

///////////////////////////////////////////////////////////////////////////////

void InitializeCLR0 (clr0_t *clr);
void ResetCLR0 (clr0_t *clr);

clr0_entry_t *AppendEntryCLR0 (clr0_t *clr, ccp name);

// number of color values a non-constant target stores: n_frames+1, because
// the animation carries an extra terminating frame like CHR0/SRT0 do
uint GetColorCountCLR0 (const clr0_t *clr);

//-----------------------------------------------------------------------------

enumError ScanRawCLR0 (clr0_t *clr, // CLR0 data structure
	bool init_clr, // true: initialize 'clr' first
	const void *data, // data to scan
	uint data_size // size of 'data'
);

//-----------------------------------------------------------------------------

enumError ScanTextCLR0 (clr0_t *clr, // CLR0 data structure
	bool init_clr, // true: initialize 'clr' first
	ccp src_fname // name of the text source to load
);

///////////////////////////////////////////////////////////////////////////////

enumError SaveRawCLR0 (clr0_t *clr, // pointer to valid CLR0
	ccp fname, // filename of destination
	bool set_time // true: set time stamps
);

//-----------------------------------------------------------------------------

enumError SaveTextCLR0 (clr0_t *clr, // pointer to valid CLR0
	ccp fname, // filename of destination
	bool set_time // true: set time stamps
);

///////////////////////////////////////////////////////////////////////////////

#endif // SZS_LIB_CLR_H
