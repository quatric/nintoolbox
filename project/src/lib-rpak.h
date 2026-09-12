#ifndef LIB_RPAK_H
#define LIB_RPAK_H

#include "lib-nintendo.h"

typedef struct rpak_entry_t
{
	const u8 *data;
	u32 size;
	u32 magic;
	u32 id_hi;
	u32 id_lo;
	bool compressed;
} rpak_entry_t;

typedef struct rpak_t
{
	const u8 *data;
	uint size;
	rpak_entry_t *entries;
	uint n_entries;
} rpak_t;

void ResetRPAK (rpak_t *pak);
enumError ScanRPAK (rpak_t *pak, const u8 *data, uint size);
u8 *DecompressRPAKEntry (const u8 *data, uint size, uint *res_size);

// Build a Retro Studios .pak (DK Country Returns, Wii) from a list of
// entries. Each entry's 'data'/'size' is the plain, uncompressed payload;
// if 'compressed' is set, it is CMPD/zlib-wrapped (falling back to a raw
// stored CMPD block if zlib doesn't shrink it, same as real retail files
// carry both kinds side by side). Writes an empty STRG section (0 strings)
// since ScanRPAK() never needed it to extract real retail archives.
// Returns an ALLOC()'d buffer (caller FREE()s it) and sets *res_size, or
// NULL on failure.
u8 *CreateRPAK (const rpak_entry_t *entries, uint n_entries, uint *res_size);

#endif
