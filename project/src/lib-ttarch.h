#ifndef LIB_TTARCH_H
#define LIB_TTARCH_H 1

#include "lib-std.h"

// Telltale Tool legacy "ttarch" archive (.ttarch; Telltale Games, e.g. Back
// to the Future: The Game, Wii port). NOT the later TTARCH2/TTCN container
// format (that one has a reversed-ASCII magic and is documented separately
// by the community; this is the older, magic-less variant).
//
// No public byte-level documentation of this legacy layout exists as a
// canonical spec, but a compatible Python parser/extractor's field layout
// (telltale-explorer, github.com/coccofresco/telltale-explorer,
// telltale/ttarch.py) was cross-checked here and confirmed byte-exact
// against every retail .ttarch file on this disc: header field values were
// hand-verified (e.g. the compressed per-chunk block-size table sums
// exactly to the declared file_data_size), the decompressed file-table
// length matches the declared header_size exactly, and actual embedded
// sub-files were extracted and confirmed to start with the expected
// Telltale "ERTM" resource magic.
//
// This implementation covers exactly what was confirmed against real files
// on this disc: version 8/9 archives (the only versions present), with
// encryption == 0 (no Blowfish; this game's archives are unencrypted) and
// files_mode 0/1 (file data stored uncompressed, directly at files_offset +
// entry.offset) or 2 (file data split into fixed-size chunks, each
// independently raw-deflate-compressed -- confirmed: each chunk decodes on
// its own, not as one continuous stream, so per-file random access only
// needs to decompress the chunk(s) actually covering that file's range).
// Encrypted archives (encryption == 1) and any other files_mode value are
// detected but not extracted, since no encryption key material or sample
// was available to confirm against.
//
// Header layout (all little-endian):
//   u32 version;            // 8 or 9 here
//   u32 encryption;         // 0 = none (only value handled), 1 = Blowfish
//   u32 unknown;            // read, not otherwise used
//   u32 files_mode;         // 0/1 = stored, 2 = chunked-compressed header+data
//   u32 chunk_count;
//   chunk_count * u32 compressed_block_size[];   // per-chunk compressed size
//   u32 file_data_size;     // == sum(compressed_block_size); sanity check
//   u32 priority, priority2;    // unused here
//   u32 xmode1, xmode2;         // unused here
//   u32 chunk_size_kb;      // chunk size in KiB (64 observed everywhere)
//   u8  unknown_byte;       // version >= 8 only
//   u32 crc32;              // version >= 9 && files_mode >= 1 only
//   u32 header_size;        // decompressed file-table size; if 0, re-read
//                            // one more u32 for the real value
//   if files_mode >= 2:
//     u32 compressed_header_size;
//     u8  compressed_header[compressed_header_size]; // raw deflate
//   else:
//     u8  header[header_size];       // stored directly
//   -- files_offset = current position; start of the data region --
//
// Decompressed file table:
//   i32 dir_count;
//   dir_count * { i32 name_len; char name[name_len]; }   // not NUL terminated
//   i32 file_count;
//   file_count * {
//       i32 name_len; char name[name_len];   // not NUL terminated
//       i32 zero;             // always 0 in every sample seen (unused here)
//       u32 offset;           // logical offset into the (decompressed, for
//                              // files_mode==2) data region
//       i32 size;
//   }
typedef struct ttarch_entry_t
{
	char name[260];
	u32 offset;
	u32 size;
} ttarch_entry_t;

typedef struct ttarch_t
{
	const u8 *raw;
	size_t raw_size;
	u32 version;
	u32 encryption;
	u32 files_mode;
	u32 chunk_size;			// bytes (0 if files_mode < 2)
	u32 chunk_count;
	const u32 *block_size;			// chunk_count entries, still points into 'raw'
	u32 files_offset;			// absolute offset of the data region
	uint n_entries;
	ttarch_entry_t *entries;
} ttarch_t;

// Parses a ttarch archive already loaded into memory. Returns ERR_OK when
// the header and file table are structurally valid (matches every check
// described above); ERR_INVALID_DATA / EINVAL otherwise. This succeeds even
// for encrypted archives or unsupported files_mode values -- callers that
// only want to know whether *extraction* is possible must check
// pod->encryption == 0 and files_mode <= 2 themselves (ExtractTtarch() does).
enumError ScanTtarch (ttarch_t *tt, const u8 *data, size_t size);
void ResetTtarch (ttarch_t *tt);

// Extracts one entry's data, allocating the output buffer. For files_mode
// 0/1 this is a plain memcpy; for files_mode 2 it decompresses exactly the
// chunk(s) spanning [entry.offset, entry.offset+entry.size).
enumError ReadTtarchEntry (const ttarch_t *tt, const ttarch_entry_t *e, u8 **data, uint *size);

// Top-level CLI extractor: loads 'arg', extracts every entry to 'basedir'
// (or --dest), preserving the archive's internal path prefix on the name.
// Refuses (ERR_NOTHING_TO_DO) for encrypted archives or an unrecognised
// files_mode, rather than guessing.
enumError ExtractTtarch (ccp arg, ccp basedir, uint depth);

#endif // LIB_TTARCH_H
