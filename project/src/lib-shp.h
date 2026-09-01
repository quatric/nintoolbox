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

// SHP0: Nintendo NW4R "vertex morph animation" BRRES sub-file.
// Added by this fork.
//
// SHP0 blends between named vertex sets of a polygon over time. Each entry
// names one polygon (material) and carries one keyframe track per morph
// target; the targets themselves are named by index into a file-level string
// list.
//
// Layout reverse-engineered from BrawlLib
// (BrawlLib/SSBB/Types/Animations/SHP0.cs: SHP0v3/SHP0v4, SHP0Entry,
// SHP0KeyframeEntries, and SHP0Node.OnInitialize for the string list).

#ifndef SZS_LIB_SHP_H
#define SZS_LIB_SHP_H 1

#include "lib-std.h"

///////////////////////////////////////////////////////////////////////////////

#define SHP0_MIN_VERSION 3
#define SHP0_MAX_VERSION 4
#define SHP0_DEFAULT_VERSION 4

///////////////////////////////////////////////////////////////////////////////
// [[shp0_key_t]]

// one keyframe of a morph track: NW4R stores (frame, value, tangent) floats
typedef struct shp0_key_t
{
	float frame;
	float value;
	float tangent;

} shp0_key_t;

///////////////////////////////////////////////////////////////////////////////
// [[shp0_track_t]]

typedef struct shp0_track_t
{
	bool is_fixed; // true: the track is a single constant, 'fixed' holds it
	float fixed;

	int vertex_idx; // index into the file-level string list (may be -1)

	float recip; // NW4R frame-scale reciprocal, preserved verbatim
	shp0_key_t *key; // alloced keyframes
	uint n_key;

} shp0_track_t;

///////////////////////////////////////////////////////////////////////////////
// [[shp0_entry_t]]

typedef struct shp0_entry_t
{
	ccp name; // alloced polygon/material name
	u32 flags; // raw flags word, preserved verbatim
	int name_idx; // raw _nameIndex field

	shp0_track_t *track; // alloced
	uint n_track;

} shp0_entry_t;

///////////////////////////////////////////////////////////////////////////////
// [[shp0_t]]

typedef struct shp0_t
{
	ccp fname; // alloced filename of loaded file
	FileAttrib_t fatt; // file attribute

	uint version; // 3 or 4
	ccp name; // alloced resource name, or NULL
	ccp orig_path; // alloced original source path, or NULL
	uint n_frames;
	bool loop;

	// The vertex-set string list. Retail SHP0 files point these offsets into
	// the *shared* BRRES string pool (the names duplicate the sibling MDL0's
	// vertex set names), so a standalone SHP0 usually cannot resolve them.
	// The raw offset is kept either way, which is what lets an unresolvable
	// file still re-encode byte-identically.
	ccp *str; // alloced; element is NULL when unresolvable
	u32 *str_off; // alloced; the raw offset as found in the file
	uint n_str;

	shp0_entry_t *entry; // alloced
	uint n_entry;
	uint n_entry_alloced;

} shp0_t;

///////////////////////////////////////////////////////////////////////////////

void InitializeSHP0 (shp0_t *shp);
void ResetSHP0 (shp0_t *shp);

shp0_entry_t *AppendEntrySHP0 (shp0_t *shp, ccp name);

//-----------------------------------------------------------------------------

enumError ScanRawSHP0 (shp0_t *shp, // SHP0 data structure
	bool init_shp, // true: initialize 'shp' first
	const void *data, // data to scan
	uint data_size // size of 'data'
);

//-----------------------------------------------------------------------------

enumError ScanTextSHP0 (shp0_t *shp, // SHP0 data structure
	bool init_shp, // true: initialize 'shp' first
	ccp src_fname // name of the text source to load
);

///////////////////////////////////////////////////////////////////////////////

enumError SaveRawSHP0 (shp0_t *shp, // pointer to valid SHP0
	ccp fname, // filename of destination
	bool set_time // true: set time stamps
);

//-----------------------------------------------------------------------------

enumError SaveTextSHP0 (shp0_t *shp, // pointer to valid SHP0
	ccp fname, // filename of destination
	bool set_time // true: set time stamps
);

///////////////////////////////////////////////////////////////////////////////

#endif // SZS_LIB_SHP_H
