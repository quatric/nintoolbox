// SPDX-License-Identifier: GPL-2.0+
// Ubisoft "Magma" engine bigfile format, as shipped on at least three Wii
// discs: Prince of Persia - Rival Swords, Cloudy with a Chance of
// Meatballs, and NCIS - Based on the TV Series. No public documentation of
// this format exists anywhere (checked XeNTaX, GBAtemp, Models-Resource,
// romhacking.net -- all empty on all three titles); everything below was
// reverse-engineered from scratch against the retail USA discs of all
// three games, cross-checked byte-for-byte against real file sizes and
// real payload headers.
//
// Two distinct on-disc pieces are covered here:
//
// (1) The ".fat" index file. This is the piece that is actually fully
//     understood and decoded. Confirmed structure, a flat, tightly packed
//     (no padding) sequence of variable-length records, one per indexed
//     sub-file, running to the end of the file:
//
//       u32 self_offset; // LE. Confirmed: always exactly equal to this
//                         // record's own byte offset from the start of
//                         // the .fat file. Verified across every record
//                         // of every sample .fat seen (Prince of Persia's
//                         // "soundwii.fat", 162 records; the byte-
//                         // identical "sound.fat" shipped on both Cloudy
//                         // with a Chance of Meatballs and NCIS, 246
//                         // records each). Redundant with the record's
//                         // real position, but real files really do carry
//                         // it, so it is decoded and cross-checked rather
//                         // than assumed.
//       u32 data_offset; // LE. Byte offset of this sub-file's payload
//                         // inside the paired bigfile.
//       u32 data_size;   // LE. Byte length of this sub-file's payload.
//       u32 zero;         // LE. Observed as exactly 0 in every record of
//                         // every sample file (hundreds of records across
//                         // all three titles). Meaning not understood
//                         // (flags? a field that just never gets used on
//                         // Wii?); reported raw rather than assumed to
//                         // always be 0.
//       u32 path_len;     // LE. Byte length of the path string that
//                         // follows, INCLUDING its terminating NUL.
//       char path[path_len]; // ASCII, forward slashes, NUL-terminated;
//                             // length matches path_len exactly.
//
//     The [data_offset, data_offset+data_size) ranges were confirmed, for
//     Prince of Persia's "soundwii.fat"/"soundwii.big" pair, to be exactly
//     contiguous and non-overlapping from 0 up to precisely the real size
//     of "soundwii.big" (489876480 bytes, 162 files, zero gap, zero
//     overlap) -- this is what pins down the field order and widths
//     unambiguously; the opposite offset/size ordering does not produce a
//     contiguous chain. Individually extracting a handful of entries at
//     their derived offsets out of "soundwii.big" also confirmed every one
//     of them starts with the same 4-byte sub-format header (bytes
//     00 19 00 03), which a plain byte-offset guess would not reliably hit
//     across 162 unrelated files.
//
//     NOT understood: the "zero" field's purpose, and whether a nonzero
//     value ever occurs on some other title. Also NOT understood: the
//     byte-identical "sound.fat" shipped on both Cloudy with a Chance of
//     Meatballs and NCIS has no corresponding "sound.big"/"sound.bf" file
//     anywhere on either disc (their actual runtime sound assets are
//     loose, individually named .SB7/.LS7/.SS7 files under files/sound/,
//     with completely different filenames than the ones inside
//     "sound.fat"). This "sound.fat" therefore looks like leftover/shared
//     middleware data that is structurally valid but was not (or is no
//     longer) wired to a real bigfile on those two titles; its
//     internal layout is still exactly the same as Prince of Persia's,
//     which is why it was used to cross-check the record format across
//     titles, but this tool cannot resolve its data_offset/data_size pairs
//     against any file that was actually found on those two discs.
//
// (2) The bigfile itself. Two on-disc variants were observed:
//       - A "flat" variant that is just the raw concatenation of payloads
//         at the offsets given by a paired ".fat" -- confirmed via Prince
//         of Persia's "soundwii.big" as described above. This is the only
//         bigfile variant this module can actually extract from.
//       - A "self-indexed" variant, seen as "pop3wii.bf" (Prince of
//         Persia), "DATA/cloudy.bf" (Cloudy with a Chance of Meatballs)
//         and "DATA/ncis.bf" (NCIS), which opens with a 16-byte header:
//           char magic[4];   // "BIG\0"
//           u32  field_a;    // LE. Observed 0x26/0x2b/0x2b (38/43/43)
//                             // across the three titles.
//           u32  field_b;    // LE. Observed 0x19f8/0x5a8/0x6a1
//                             // (6648/1448/1697).
//           u32  field_c;    // LE. Observed 5/5/6.
//         followed immediately by a run of 0xff bytes and then a much
//         denser binary table that was NOT reverse-engineered: it is not
//         the same 20-byte-header/NUL-path record shape as the ".fat"
//         format above (entries here are packed far tighter and mix in
//         what look like additional offset/pointer fields and repeated
//         sentinel words such as 0xffffffff), and none of the three
//         paired ".fat" files found on these discs decode correctly
//         against a plain byte offset into their game's own ".bf" (the
//         payload bytes at a ".fat" entry's data_offset inside e.g.
//         "cloudy.bf" are not a valid sub-file header). Whatever
//         relationship exists between a game's ".fat" files and its
//         ".bf" (a nested sub-region base offset? A completely separate,
//         unlinked index into the same ".bf"? An unrelated legacy
//         leftover?) was not determined. This module therefore only
//         magic/structure-detects this variant (IsMagmaBigfile()) and
//         does not attempt to decode or extract from it.
#ifndef LIB_MAGMA_H
#define LIB_MAGMA_H 1

#include "lib-std.h"

//-----------------------------------------------------------------------------
// ".fat" index: structural detection (no fixed magic bytes -- the format
// is validated by checking that the whole buffer parses as a tightly
// packed run of the record shape described above, with self_offset fields
// matching their own real position and non-corrupt/printable paths).
//
// 'size' is how much of the file is actually available at 'data'; pass the
// real file size for both 'size' and 'file_size' when the whole file is
// already loaded (decode path). A short FILETYPE probe buffer is accepted
// too, as long as it covers at least one full record.
int IsMagmaFat (const u8 *data, size_t size, size_t file_size);

// Dumps every record of the ".fat" index: self_offset (cross-checked),
// data_offset, data_size, the not-understood "zero" field (reported raw),
// and the path.
enumError DecodeMagmaFat_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// ".bf" self-indexed bigfile: magic + light header sanity only. See the
// big comment above for exactly what is and is not understood about its
// internal table; this module does not decode or extract from it.
int IsMagmaBigfile (const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// Extracts every sub-file listed in a ".fat" index out of a paired flat
// bigfile (the variant confirmed via Prince of Persia's "soundwii.fat" /
// "soundwii.big"; see above -- this does NOT work against the
// self-indexed ".bf" variant). 'fat_path' is the ".fat" file itself;
// 'data_path' is the already-resolved path of the paired flat data file
// (caller is responsible for locating it, e.g. by trying the ".big"/".bf"
// sibling of the ".fat" basename); 'dest_dir' is the directory sub-files
// are written under, recreating each entry's own relative path.
enumError ExtractMagmaFat
(
	ccp		fat_path,	// ".fat" index file to read
	ccp		data_path,	// paired flat data file to extract from
	ccp		dest_dir	// destination directory (created as needed)
);

#endif // LIB_MAGMA_H
