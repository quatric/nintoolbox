
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

// VIS0: Nintendo NW4R "bone/node visibility animation" BRRES sub-file.
// Added by this fork. VIS0 stores, per named scene-graph node (matched by
// name against a sibling MDL0's bones/nodes), either
//   - a "constant" flag (always visible / always hidden, no per-frame data)
//   - or a bit per animation frame ("visible this frame: yes/no"),
// packed 8 bits/byte, big endian bit order (MSB = frame 0 of that byte),
// and padded up to a multiple of 32 frames per entry.
//
// Binary layout reverse-engineered from BrawlLib
// (BrawlLib/SSBB/Types/Animations/VIS0.cs, VIS0Node.cs): header versions 3
// (no user data) and 4 (adds a trailing user-data offset), followed by the
// standard BRRES resource-group (dummy root entry #0xffff + N named
// entries), each entry either 8 bytes (constant: flags only) or
// 8 + ceil(numFrames,32)/8 bytes (animated: flags + bit array).

#ifndef SZS_LIB_VIS0_H
#define SZS_LIB_VIS0_H 1

#include "lib-std.h"

///////////////////////////////////////////////////////////////////////////////

#define VIS0_MIN_VERSION 3
#define VIS0_MAX_VERSION 4
#define VIS0_DEFAULT_VERSION 4

// [[vis0_entry_t]]

typedef struct vis0_entry_t
{
	ccp name; // alloced entry (bone/node) name
	bool is_constant; // true: no per-frame data, just 'enabled'
	bool enabled; // only relevant if 'is_constant'
	u8 *bits; // NULL if constant, else 1 bit/frame, MSB first,
			  //   ceil(n_frames,32)/8 bytes, alloced
	uint n_bits_byte; // size of 'bits' in bytes

} vis0_entry_t;

///////////////////////////////////////////////////////////////////////////////
// [[vis0_t]]

typedef struct vis0_t
{
	ccp fname; // alloced filename of loaded file
	FileAttrib_t fatt; // file attribute

	uint version; // 3 or 4
	ccp name; // alloced resource name of the animation, or NULL
	ccp orig_path; // alloced original source path, or NULL
	uint n_frames; // number of frames
	bool loop; // true: animation loops

	vis0_entry_t *entry; // list of entries, alloced
	uint n_entry; // number of used elements of 'entry'
	uint n_entry_alloced; // number of alloced elements of 'entry'

} vis0_t;

///////////////////////////////////////////////////////////////////////////////

void InitializeVIS0 (vis0_t *vis);
void ResetVIS0 (vis0_t *vis);

vis0_entry_t *AppendEntryVIS0 (vis0_t *vis, ccp name);

//-----------------------------------------------------------------------------

enumError ScanRawVIS0 (vis0_t *vis, // VIS0 data structure
	bool init_vis, // true: initialize 'vis' first
	const void *data, // data to scan
	uint data_size // size of 'data'
);

//-----------------------------------------------------------------------------

enumError ScanTextVIS0 (vis0_t *vis, // VIS0 data structure
	bool init_vis, // true: initialize 'vis' first
	ccp src_fname // name of the text source to load
);

///////////////////////////////////////////////////////////////////////////////

enumError SaveRawVIS0 (vis0_t *vis, // pointer to valid VIS0
	ccp fname, // filename of destination
	bool set_time // true: set time stamps
);

//-----------------------------------------------------------------------------

enumError SaveTextVIS0 (vis0_t *vis, // pointer to valid VIS0
	ccp fname, // filename of destination
	bool set_time // true: set time stamps
);

///////////////////////////////////////////////////////////////////////////////

#endif // SZS_LIB_VIS0_H
