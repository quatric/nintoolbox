#ifndef LIB_TRPAK_H
#define LIB_TRPAK_H

#include "lib-nintendo.h"
#include <stdio.h>

// Nintendo Switch "tr Package" archive (.trpak), used by some newer Koei Tecmo Switch ports
// (e.g. Fire Emblem Warriors: Three Hopes -- the ".trmdl"/".trmbf"/".trskl" model files in
// KillzXGaming/Switch-Toolbox's own CategoryLookup are this game's). Reference:
// KillzXGaming/Switch-Toolbox File_Format_Library/FileFormats/Archives/TRPAK/TRPAK.cs, which
// only wraps the FlatBuffers-generated accessor in the sibling Flatbuffers/TRPAK.cs -- that
// generated file is the actual wire-layout reference used here, since it encodes the compiled
// schema's vtable field order/ids directly (there is no checked-in .fbs for this format).
//
// This is a real (if minimal) FlatBuffers root table, not a fixed-offset struct:
//   root:  u32 root_offset;                       // @0, relative to offset 0
//   table  TRPAK  { hashes:[ulong]; files:[File]; }        // field ids 0,1 -> vtable slots 4,6
//   table  File   { unused:byte; compression_type:byte=255; unk1:byte;
//                   decompressed_size:ulong; data:[byte]; } // ids 0..4 -> vtable slots 4,6,8,10,12
// 'hashes' and 'files' are parallel arrays (same length in every real file; the reference loader
// throws if they differ) -- hashes[i] is a lookup key into an external, game-supplied name
// database this project does not have, so decoded/extracted entries are named by index+hash
// instead of a real path (there is also no archive-supplied string field to path-sanitize: the
// only per-entry identifier here is a plain u64 hash, never a string).
//
// FlatBuffers' vtable/offset chain (root offset -> table soffset -> vtable -> field uoffset ->
// vector length/data) is walked field-by-field in lib-trpak.c with every hop bounds-checked at
// 64-bit width before use, the same discipline as this project's other offset-heavy decoders.

typedef struct trpak_entry_t
{
	u64 hash; // File.Compression's game-side lookup key (FNV64A1 of a real path)
	u8 compression; // TRPAK Compression enum: 3 = OODLE, 255 = NONE, else unrecognized
	u8 unused, unk1; // File.Unused / File.Unk1, meaning unknown upstream too
	u64 decompressed_size; // File.DecompressedSize (only meaningful when compression == OODLE)
	u64 data_offset; // absolute offset of File.Data's raw bytes within the source buffer
	u32 data_size; // File.Data's raw (possibly still-compressed) byte length
} trpak_entry_t;

typedef struct trpak_t
{
	const u8 *data; // the buffer that was scanned (not owned)
	uint size;
	uint n_entries;
	trpak_entry_t *entries; // owned
} trpak_t;

// Structural check only (TRPAK has no magic bytes at all -- Switch-Toolbox's own Identify()
// keys purely off the ".trpak" extension): true if 'data' parses as a FlatBuffers root table
// whose root offset, table vtable, and field offsets are all self-consistent, plausible enough
// that a corrupt/foreign file is very unlikely to satisfy it by chance.
bool IsTRPAK (const u8 *data, size_t size);

// Walks the FlatBuffers root table into a flat entry list. Fails closed (ERR_INVALID_DATA) on
// any out-of-bounds offset/length anywhere in the vtable/vector chain, or on a hashes/files
// length mismatch (a corrupt file, per the reference loader's own invariant).
enumError ScanTRPAK (trpak_t *trpak, const u8 *data, uint size);
void ResetTRPAK (trpak_t *trpak);

// Text manifest: one line per entry with its index, hash, compression type, raw/decompressed
// size and the raw byte offset+length of its data blob within the source file.
enumError DecodeTRPAK_Text (FILE *out, const trpak_t *trpak);

#endif // LIB_TRPAK_H
