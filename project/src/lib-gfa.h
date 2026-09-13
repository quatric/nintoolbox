#ifndef LIB_GFA_H
#define LIB_GFA_H

#include "lib-nintendo.h"

typedef struct ParamField_t ParamField_t; // see dclib-basics.h

typedef struct gfa_entry_t
{
	ccp name;
	u32 offset;
	u32 size;
} gfa_entry_t;

typedef struct gfa_t
{
	u8 *blob;
	uint blob_size;
	gfa_entry_t *entries;
	uint n_entries;
	char *names;
	uint compression;
} gfa_t;

void ResetGFA (gfa_t *gfa);
enumError ScanGFA (gfa_t *gfa, const u8 *data, uint size);
enumError CreateGFA (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries,
	uint compression, // GFCP compression id to re-encode with: 1=BPE, 2 or 3=raw LZ10.
		// Any other value (including 0, "unknown") falls back to LZ10 (3), the
		// long-standing default -- but a caller repacking an existing archive
		// should always pass the id it actually read back from that archive's
		// own GFCP header (see peek_gfa_compression() in compress.inc), not 0,
		// or every re-CREATE of a BPE-compressed source silently converts it to
		// LZ10 even though nothing about its content changed.
	const ParamField_t *hash_hint); // NULL, or a name->value table (see
		// ReadGFAHashHints()) supplying the opaque per-entry value that goes
		// in each 16-byte record's first 4 bytes. That value isn't a
		// recomputable name hash (verified against retail files) and its
		// real meaning is unknown, but zeroing it -- the only option without
		// a hint -- has been observed to make the game crash accessing the
		// repacked content, so a caller repacking an existing archive should
		// always supply the hints read from the file being replaced. Entries
		// with no matching name in the table get 0, same as before.
enumError PeekGFACompression (ccp path, uint *compression); // read just the
	// GFCP compression id from an existing on-disk .gfa file, without
	// decompressing its payload; returns an error (and leaves *compression
	// untouched) if 'path' isn't readable or isn't a valid GFAC file.
enumError ReadGFAHashHints (ccp path, ParamField_t *out); // read the
	// per-entry opaque rec+0 values (see CreateGFA's 'hash_hint' doc) from an
	// existing on-disk .gfa file into 'out' (already-initialized, keyed by
	// entry name), without decompressing the payload. Returns an error (and
	// leaves 'out' untouched) if 'path' isn't readable or isn't a valid
	// GFAC file.

enumError DecodeLZ10Raw (u8 *dest, uint dest_size, const u8 *src, uint src_size);
enumError EncodeLZ10Raw (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError DecodeBPE (u8 *dest, uint dest_size, const u8 *src, uint src_size);
enumError EncodeBPE (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

#endif
