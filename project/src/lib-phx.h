#ifndef LIB_PHX_H
#define LIB_PHX_H

#include "lib-nintendo.h"

// Kirby Air Riders' "phx" engine asset packs (Switch 2):
//   Asset/assets.cat  -- "PARC" catalog: 64-bit asset IDs, per-asset block lists
//                        ("Main", "HEAD", "BODY", "GBuf", "$DEP", ...) and a location
//                        table {pack, codec (0 raw / 1 zstd), offset, sizes, SHA-256}
//   Asset/assets_*.dat -- "PDAT" packs holding the blocks
// Records are flatbuffers whose identifier at +4 names the type (PTEX, PMSH,
// PANM, PSND, PMTR, PMAP, PEFF, ...). Names are hashed; files are named by ID.

bool IsPARC (const u8 *data, uint size);
enumError ExtractPARCArchive (ccp arg, ccp basedir, uint depth);

// PTEX texture: the HEAD flatbuffer (".ptex") plus its BODY (".ptex.body") -> RGBA.
bool IsPTEX (const u8 *data, uint size);
enumError DecodePTEX_RGBA (u8 **dest, uint *width, uint *height, const u8 *head, uint head_size,
	const u8 *body, uint body_size, ccp *format_name);

#endif // LIB_PHX_H
