#ifndef SZS_LIB_VC_H
#define SZS_LIB_VC_H 1

#include "types.h"

// Wii Virtual Console formats: CCF (a general archive container, optional
// per-entry zlib compression) and "romc"/"romchu" (N64 VC's own ROM
// compression, applied directly to the ROM -- NOT wrapped in CCF on every
// real sample checked so far; see the long comment above DecodeRomC() in
// lib-vc.c for what's actually been observed in real retail WADs).

//-----------------------------------------------------------------------------
// CCF ("CCF\0" magic): WiiBrew-documented flat archive, optional per-entry
// zlib (standard, not raw-deflate) compression.

typedef struct ccf_entry_t
{
	char name[21]; // NUL-terminated copy of the 20-byte name field
	const u8 *data; // points into the source buffer (still compressed
					// if compressed != decompressed size)
	u32 size; // stored (possibly compressed) size
	u32 decompressed_size; // == size if stored raw
} ccf_entry_t;

typedef struct ccf_t
{
	const u8 *data;
	uint size;
	uint n_entries;
	ccf_entry_t *entries; // owned
} ccf_t;

enumError ScanCCF (ccf_t *ccf, const u8 *data, uint size);
void ResetCCF (ccf_t *ccf);

// Decompresses one CCF entry (a no-op copy if it's stored raw).
enumError DecodeCCFEntry (u8 **dest, uint *dest_size, const ccf_entry_t *entry);

struct nintendo_sarc_entry_t;
enumError CreateCCF (u8 **dest, uint *dest_size, const struct nintendo_sarc_entry_t *entries,
	uint n_entries, bool compress);

//-----------------------------------------------------------------------------
// "romc"/"romchu": N64 Virtual Console's own ROM compression, applied
// directly to a ROM file (own 4-byte header, no CCF wrapper on any real
// sample found). See lib-vc.c for the header layout and what's verified.
// Compression type 1 (plain LZ77/LZ10) is decoded and encoded; type 2
// ("romchu", LZ77+Huffman) is detected and declined (EINVAL) rather than
// guessed at -- no real sample of it has been found yet.

enumError DecodeRomC (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeRomC (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

#endif
