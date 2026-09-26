// SPDX-License-Identifier: GPL-2.0+
#ifndef LIB_FPK_H
#define LIB_FPK_H 1

#include "lib-std.h"

// Traveller's Tales "FPK" resource package (.fpk), reused across several TT
// titles (LEGO Star Wars, LEGO Indiana Jones, and -- confirmed here --
// Bionicle Heroes / Eidos-TT's pre-LEGO engine). No public spec was found;
// web search turned up only forum mentions of the extension, no documented
// layout or field sizes, so everything below was reverse-engineered from
// scratch against the retail USA "Bionicle Heroes" disc's own
// DATA/files/audio/*_ngc.fpk packages and cross-checked across all 25 of
// them. All fields are little-endian despite the rest of the (big-endian,
// GameCube/Wii) disc -- these packages are PC-authored TT tool output,
// carried over byte-for-byte onto the Wii disc.
//
// Layout, confirmed by direct hexdump + cross-checking name/offset/size
// fields against the real extracted entries:
//   0x00  u32 LE magic = 0x1234567A
//   0x04  u32 LE entry_count
//   0x08  u32 LE total_file_size (equals the package's own file size)
//   0x0c  u32 LE unknown (varies per file; not a simple sum/count we could
//         match -- treated as an opaque checksum/hash, never relied on)
//   0x10  u32 LE 0 (reserved, always zero in every sample)
//   0x14  u32 LE 0xCCCCCCCC (constant sentinel/padding in every sample)
//   0x18  entry_count * 28-byte records:
//           u32 LE name_offset   -- absolute file offset of a NUL-terminated
//                                    ASCII path (e.g.
//                                    "audio\_soundfx\_const\hub\Hub_head_key_u.dsp"),
//                                    using the *original* backslash path as
//                                    authored, not just a leaf filename
//           u32 LE data_offset   -- absolute file offset of the entry's data
//           u32 LE data_size     -- byte length of the entry's data
//           u32 LE type          -- constant 16 (0x10) in every entry seen
//                                    across all 25 sample packages; likely a
//                                    resource-type id, but only one value is
//                                    attested so its meaning is unconfirmed
//           u32 LE reserved[3]   -- always zero in every sample
//   The name table (one NUL-terminated string per entry, tightly packed, no
//   padding) starts immediately after the entry table, at exactly
//   0x18 + entry_count*28 -- confirmed to match entry[0].name_offset in
//   every sample package.
//
// Payload data observed so far is never compressed (no zlib 0x78 magic at
// any data_offset in any sample): the .fpk packages checked all contain raw
// Nintendo DSP-ADPCM streams (.dsp, matching the disc's own top-level .dsp
// files byte-for-byte in header shape). Packages containing other payload
// types (e.g. the .gcm-extension audio elsewhere on this disc, or non-audio
// resources in other TT titles) were not examined, so compression support
// for those is NOT implemented -- entries are always extracted raw.
typedef struct fpk_entry_t
{
	u32 name_offset;
	u32 data_offset;
	u32 data_size;
	u32 type;
	char name[261]; // path as stored, truncated defensively
} fpk_entry_t;

typedef struct fpk_t
{
	const u8 *raw;
	size_t raw_size;
	uint n_entries;
	fpk_entry_t *entries;
} fpk_t;

enumError ScanFPK (fpk_t *fpk, const u8 *data, size_t size);
void ResetFPK (fpk_t *fpk);
enumError ExtractFPKArchive (ccp arg, ccp basedir, uint depth);

#endif // LIB_FPK_H
