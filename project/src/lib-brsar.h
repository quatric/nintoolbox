#ifndef SZS_LIB_BRSAR_H
#define SZS_LIB_BRSAR_H 1

#include "lib-std.h"

// BRSAR/BFSAR/BCSAR (Wii/Wii U/3DS Nintendo SoundArchive) packer + unpacker.
//
// RSAR (Wii, "RSAR") field layout provenance: every offset used for that
// variant was taken directly from the read side already vendored in this
// repo (vgmtrans' RSARScanner.cpp, RSAR::ReadSoundTable/ReadBankTable/
// ReadFileTable/ReadGroupTable/ParseSymbBlock/Parse), not invented. Fields
// that reader never touches (sound 3D params, extended player info, the
// SYMB name-lookup trie, and the internal structure of RBNK/RWAR/RWSD
// payloads) are written as zero or passed through opaquely.
//
// FSAR/CSAR (Wii U "FSAR" / 3DS "CSAR") are NOT covered by any reference
// implementation in this repo or in the sibling 'mobipeg' repo -- neither
// vgmtrans nor mobipeg's FFmpeg fork parses these archive containers (only
// the sibling BFSTM/BCSTM *stream* format is real there). Their layout
// below is an extrapolation: the real, vgmtrans-verified SYMB/INFO/FILE
// content bytes are reused unchanged, wrapped in the section-table envelope
// convention that BFSTM/BCSTM are confirmed to actually use (0x4000/0x4001/
// 0x4002-flagged section entries, big-endian FSAR / little-endian-default
// CSAR). This is self-consistent and round-trip tested by this file's own
// pack/unpack pair, but it is NOT independently verified against any real
// FSAR/CSAR file or reader -- treat it as best-effort, not ground truth.
//
// Only RSEQ sound entries and RBNK bank entries are modeled as first-class
// INFO records (both have a real name<->fileID path in the vendored
// reader); RWAR/RWSD assets are packed into the FILE block as opaque
// pass-through blobs with no recoverable name (real RSAR doesn't expose one
// at this level either -- names for those live inside RWSD's own region
// info, out of scope). Only a single archive group is produced.

typedef enum brsar_variant_t
{
	BRSAR_VARIANT_RSAR, // Wii,   "RSAR" -- verified against vgmtrans' reader
	BRSAR_VARIANT_FSAR, // Wii U, "FSAR" -- extrapolated, see header comment above
	BRSAR_VARIANT_CSAR, // 3DS,   "CSAR" -- extrapolated, see header comment above
} brsar_variant_t;

typedef enum brsar_asset_type_t
{
	BRSAR_ASSET_RSEQ, // MML text (assembled via AssembleSequence) or raw .rseq/.brseq binary
	BRSAR_ASSET_RBNK, // opaque instrument bank blob
	BRSAR_ASSET_RWAR, // opaque wave archive blob
	BRSAR_ASSET_RWSD, // opaque wave sound data blob
} brsar_asset_type_t;

typedef struct brsar_asset_t
{
	ccp name; // symbol / label (e.g. "seq_boss01")
	brsar_asset_type_t type;
	const u8 *data; // asset payload (already-assembled binary)
	size_t size;
	u32 bank_id; // RSEQ only: index into the bank table it references
	const u8 *wave_data; // RBNK/RWSD only: paired RWAR wave data (group wave region), or NULL
	size_t wave_size;
} brsar_asset_t;

// One sound-table entry. A real RSEQ file holds many labeled songs, so several
// sounds may reference the same RSEQ asset at different label offsets.
typedef struct brsar_sound_t
{
	ccp name; // sound name (e.g. "BGM_TOWN")
	ccp seq_name; // asset name of the RSEQ it plays
	ccp bank_name; // asset name of the RBNK it uses (NULL = bank 0)
	u32 data_offset; // label offset inside the RSEQ DATA block
	u32 alloc_track; // track allocation mask
} brsar_sound_t;

// Build an archive binary in memory from a list of pre-resolved assets.
// Sequence assets must already be assembled binary (RSEQ/BRSEQ); text/MML
// sources are the caller's responsibility to run through AssembleSequence()
// first (see wbrsar.c's pack command).
enumError PackBRSAR (u8 **out_data, size_t *out_size, const brsar_asset_t *assets, uint n_assets,
	brsar_variant_t variant);

// Like PackBRSAR, but with an explicit sound table (see brsar_sound_t). With
// n_sounds == 0 every RSEQ asset becomes one sound named after itself.
enumError PackBRSAREx (u8 **out_data, size_t *out_size, const brsar_asset_t *assets, uint n_assets,
	const brsar_sound_t *sounds, uint n_sounds, brsar_variant_t variant);

// RWAR wave archive <-> directory of RWAV files (NNNNN.brwav). Repacking is
// content-preserving; padding is normalized to 0x20.
enumError UnpackRWAR (const u8 *data, size_t size, ccp out_dir);
enumError PackRWARDir (u8 **out_data, size_t *out_size, ccp in_dir);

// Scan a directory for RSEQ (.txt MML source, .rseq/.brseq binary) and
// RBNK/RWAR/RWSD files, assemble/load them, and build an archive.
enumError PackBRSARDir (u8 **out_data, size_t *out_size, ccp input_dir, brsar_variant_t variant);

// Extract every asset from an archive binary (any variant, auto-detected)
// into 'out_dir' as individual files. RSEQ/RBNK entries recover their real
// name from the SYMB/sound/bank tables. Archives written by this library also
// mark a file-entry name association for RWAR/RWSD; unrelated retail entries
// without a name use "file_NNN.<ext>". Extensions are sniffed from magic.
enumError UnpackBRSAR (const u8 *data, size_t size, ccp out_dir);

// BrawlCrate-style extraction: sequences as .brseq, banks as .brbnk, wave sets
// as .brwsd, and every group's wave data as a paired <name>.brwar next to its
// bank/wave-set. sounds.tsv records the sound table (name, sequence file,
// label offset, bank, track mask) so PackBRSARDir restores it. With
// 'recursive', each .brwar becomes a <name>.brwar.d/ directory of .brwav files
// (PackBRSARDir rebuilds the .brwar from it).
enumError UnpackBRSAREx (const u8 *data, size_t size, ccp out_dir, bool recursive);

#endif // SZS_LIB_BRSAR_H
