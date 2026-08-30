/***************************************************************************
 *                                                                         *
 *   HAL Laboratory "A2" bank archive support (Kirby Air Ride)              *
 *                                                                         *
 ***************************************************************************/

#ifndef LIB_HALBANK_H
#define LIB_HALBANK_H

#include "lib-std.h"
#include <stdint.h>
#include <stddef.h>

//
///////////////////////////////////////////////////////////////////////////////
///////////////		    HAL "A2" bank archive		///////////////
///////////////////////////////////////////////////////////////////////////////
//
// Kirby Air Ride ships two different HAL ".dat" flavours side by side in the
// same /files directory.  The 3D/game-object ones are sysdolphin object
// graphs and are handled by lib-hsd.c; the "A2*" ones (the 2D/UI banks) are
// NOT -- they are a much simpler named-blob archive:
//
//	+0x00  u32  file size, or 0 (both spellings occur in retail)
//	+0x04  u32  entry count N
//	+0x08  N * { u32 name offset; u32 data offset }	 -- absolute, from SOF
//	+0x08+8N    NUL-terminated name string pool
//	...         the blobs, each 0x20-aligned
//
// Every blob is prefixed by its own u32 byte size, stored in the four bytes
// *before* the data offset the pair table names; i.e. the payload itself is
// what is 0x20-aligned and the size word sits in the alignment gap.
//
// There is no type tag: an entry's kind and, for textures, its pixel format
// and dimensions are encoded in the entry name, e.g.
//
//	replay.C8RGB5A3_160_40.tex	160x40, CI8 indices + RGB5A3 palette
//	shadow.RGBA8_64_64.tex		64x64 RGBA32
//	arrow_r.12_12_12_12.C8RGB5A3_24_24.tex	   ditto, with 9-slice margins
//	ScInfCenter2D.tm		a 2D scene blob (not handled here)
//
// The payload of a ".tex" entry is the raw GX texture data followed, for the
// colour-indexed formats, by the palette -- nothing else, no per-blob header.
//
// This layout was determined by direct inspection of retail bytes; see the
// verification notes in lib-halbank.c.
//
///////////////////////////////////////////////////////////////////////////////

// [[halbank_entry_t]]
typedef struct halbank_entry_t
{
	ccp name; // into 'data', NUL terminated, not owned
	u32 data_off; // absolute offset of the payload
	u32 data_size; // payload size, up to the next payload / EOF
} halbank_entry_t;

// [[halbank_t]]
typedef struct halbank_t
{
	const u8 *data; // whole file, not owned
	uint size; // size of 'data'
	halbank_entry_t *entry; // 'n_entry' parsed entries
	uint n_entry;
} halbank_t;

///////////////////////////////////////////////////////////////////////////////

// Quick structural probe: an A2 bank has no magic either, so this validates
// the pair table (see IsHALBank() in lib-halbank.c for the exact criteria).
bool IsHALBank (const u8 *data, uint size);

// Parse the pair table. Returns false if 'data' is not an A2 bank.
// 'data' is borrowed and must outlive 'bank'. Call ResetHALBank() when done.
bool ScanHALBank (halbank_t *bank, const u8 *data, uint size);
void ResetHALBank (halbank_t *bank);

// Decode every ".tex" entry and write it as a PNG into 'dest_dir', named
// "<basename>.<entry>_<W>x<H>_<format>.png".
// Returns the number of textures written, or -1 on error.
int ExportHALBankTextures (const halbank_t *bank, // valid, scanned bank
	ccp dest_dir, // output directory, created if needed
	ccp basename // name prefix for the written PNGs
);

// Convenience wrapper: scan 'data' and export its textures.
int ExportHALBankTexturesFromData (const u8 *data, uint size, ccp dest_dir, ccp basename);

#endif // LIB_HALBANK_H
