// SPDX-License-Identifier: GPL-2.0+
// Monster 4x4: Stunt Racer (Wii) "CHNK" chunked container format. The same
// container is reused, unchanged, across at least eight different
// extensions shipped on the retail USA disc: .bnk (font/audio bank), .d4l
// (litter/prop layout), .d4c (city/track geometry), .gfx (texture atlas,
// note: on-disc extension is upper-case ".GFX"), .mec (menu/GUI layout,
// ".MEC"), .pmu (sound mixer bank, ".PMU"), .pak (generic resource pack),
// .ppx (particle effects) and .an4 (animation clip). No public
// documentation of this container exists anywhere (checked XeNTaX, GBAtemp,
// Models-Resource, romhacking.net -- all empty on this title); everything
// below was reverse-engineered from scratch against dozens of files of
// every listed extension on the retail disc, cross-checked byte-for-byte
// against each file's own on-disk size.
//
// What is understood and confirmed here:
//   - A fixed 16-byte file header: magic "CHNK", u32 total_file_size (LE),
//     u32 chunk_count (LE), u32 format_version (LE, always observed as 3).
//     total_file_size was confirmed to equal the real on-disk file size,
//     byte for byte, across every sample file of every one of the eight
//     extensions above (dozens of files, sizes ranging from 24KB to
//     17.7MB) -- this is what IsCHNK() gates on, since it is exact and
//     essentially impossible to satisfy by chance for non-CHNK data.
//   - A flat top-level chunk table immediately following the header, one
//     16-byte entry per chunk_count, no padding between entries or between
//     the table and the header:
//       char tag[4];      // ASCII chunk tag, e.g. "BINF", "GEGR", "LTCK"
//       u32  data_offset; // LE, byte offset from start of file
//       u16  field_b;     // LE, meaning not understood (see below)
//       u16  field_c;     // LE, meaning not understood (see below)
//       u32  data_size;   // LE, byte length of this chunk's payload
//     data_offset/data_size were confirmed by construction: for every
//     sample file, every entry's [data_offset, data_offset+data_size)
//     range lies inside the file, the ranges for entries in a single file
//     never overlap, and the highest range's end lines up closely with
//     total_file_size (typically to within one alignment quantum). The
//     order of the offset/size fields within the 16-byte entry (as opposed
//     to the reverse) was pinned down by the fact that only this ordering
//     produces monotonically-nondecreasing, non-overlapping ranges across
//     every multi-entry sample seen (e.g. an 11-chunk track file); the
//     opposite ordering produces nonsensical overlapping/backwards ranges.
//   - Between the chunk table and the first chunk's payload, unused space
//     is padded with the repeating filler byte 0x3e ('>').
//
// What is explicitly NOT understood or decoded, and is deliberately left
// unparsed rather than guessed at:
//   - field_b and field_c in each table entry. They are stable per tag in
//     some files (e.g. many BINF/GEIR/GEGR entries carry field_c == 0x0b)
//     but vary across others in ways not explained by any hypothesis tried
//     (chunk-type enum, flags, sub-version, element count); reported raw.
//   - The internal layout of every chunk payload (BINF, GEGR, GEIR, GEGL,
//     LTCK, PTCK, LECK, FONT, SSIC, etc.). These are almost certainly
//     structured (geometry, texture, audio, or layout data respectively)
//     but no attempt was made to decode any of them; each is reported only
//     by tag, offset and size. A chunk's tag can itself recurse into a
//     nested sub-table using this same 16-byte entry shape (observed
//     informally but not verified/implemented), so nested tags are also
//     not expanded here.
#ifndef LIB_MONSTER4X4_H
#define LIB_MONSTER4X4_H 1

#include "lib-std.h"

//-----------------------------------------------------------------------------
// "CHNK" container: magic-based detection.
//
// 'file_size' is the true total size of the file being probed; 'size' is
// how much of it is actually available at 'data' (a FILETYPE probe may only
// hand over a short prefix). Pass file_size == size when the full file is
// already loaded (decode path).
int IsCHNK (const u8 *data, size_t size, size_t file_size);

// Dumps the file header and the full top-level chunk table (tag, offset,
// size, and the two not-understood fields reported raw). Chunk payloads
// are never decoded, only located.
enumError DecodeCHNK_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // LIB_MONSTER4X4_H
