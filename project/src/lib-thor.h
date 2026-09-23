#ifndef LIB_THOR_H
#define LIB_THOR_H

#include "lib-std.h"

// Behaviour Interactive "Thor" resource package (.wii), used by
// Phineas and Ferb: Quest for Cool Stuff (Wii). Not a Wii disc image --
// despite the ".wii" extension it is a level/asset container that lives
// inside the extracted disc filesystem at DATA/files/phinferb/<level>/
// <level>.wii and <level>_en.wii (localized variant). See lib-thor.c for
// the confirmed layout.
//
// "Thor" is a literal ASCII marker embedded at a fixed offset in every
// sample (offset 0x3C) -- most likely a build-tool/engine signature left
// by Behaviour Interactive's asset pipeline (main.dol strings reference
// paths like "z:\generated\thor\phineas_ferb\wii\..."). No public
// documentation of this container was found, so the name is adopted
// from that marker.

typedef struct thor_entry_t
{
	u32 id;			// packed type/index id, meaning not fully decoded
	u32 offset;		// absolute file offset of the resource blob
	u32 size;		// size of the resource blob
} thor_entry_t;

typedef struct thor_t
{
	const u8 *raw;
	size_t raw_size;
	char name[13];		// header name field (e.g. "garage06")
	char category[13];	// header category field (e.g. "phinferb")
	u32 table_offset;	// absolute offset of the resource table
	uint n_entries;
	thor_entry_t *entries;
} thor_t;

bool IsThorPkg (const u8 *data, size_t size);
enumError ScanThorPkg (thor_t *thor, const u8 *data, size_t size);
void ResetThorPkg (thor_t *thor);
enumError ExtractThorPkg (ccp arg, ccp basedir, uint depth);

#endif
