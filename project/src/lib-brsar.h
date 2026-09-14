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
} brsar_asset_t;

// Build an archive binary in memory from a list of pre-resolved assets.
// Sequence assets must already be assembled binary (RSEQ/BRSEQ); text/MML
// sources are the caller's responsibility to run through AssembleSequence()
// first (see wbrsar.c's pack command).
enumError PackBRSAR (u8 **out_data, size_t *out_size, const brsar_asset_t *assets, uint n_assets,
	brsar_variant_t variant);

// Scan a directory for RSEQ (.txt MML source, .rseq/.brseq binary) and
// RBNK/RWAR/RWSD files, assemble/load them, and build an archive.
enumError PackBRSARDir (u8 **out_data, size_t *out_size, ccp input_dir, brsar_variant_t variant);

// Extract every asset from an archive binary (any variant, auto-detected)
// into 'out_dir' as individual files. RSEQ/RBNK entries recover their real
// name from the SYMB/sound/bank tables. Archives written by this library also
// mark a file-entry name association for RWAR/RWSD; unrelated retail entries
// without a name use "file_NNN.<ext>". Extensions are sniffed from magic.
enumError UnpackBRSAR (const u8 *data, size_t size, ccp out_dir);

#endif // SZS_LIB_BRSAR_H
