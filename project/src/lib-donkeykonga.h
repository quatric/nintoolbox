// SPDX-License-Identifier: GPL-2.0+
// Donkey Konga (GameCube, all three retail discs: Donkey Konga [USA],
// Donkey Konga 2 [USA], Donkey Konga 3 - Tabehoudai! Haru Mogitate 50-kyoku
// [Japan]) proprietary asset formats. No public documentation of either
// format below exists. Both were reverse-engineered from scratch against
// all three retail discs, cross-checking every real sample of each
// extension byte-for-byte.
//
// (1) ".tpl.dkz" -- "DKZF" trivial zlib texture wrapper. Donkey Konga
//     (disc 1) only: every one of 64 real `files/**/*.tpl.dkz` samples on
//     that disc decodes cleanly. Donkey Konga 2 and 3 ship their textures
//     as plain `.nut` files instead and carry no `.dkz` files at all, so
//     this wrapper is confirmed disc-1-only, not merely untested elsewhere.
//     Layout, all fields big-endian:
//       char magic[4];         // "DKZF"
//       u32  decompressed_size;
//       u8   zlib_stream[];    // standard raw zlib stream (0x78 0x?? magic)
//     Inflating the stream reproduces the wrapped file exactly (confirmed:
//     decompressed size matches the header field and the output always
//     starts with the Nintendo TPL magic `00 20 AF 30` in every one of the
//     64 samples -- this disc only ever wraps `.tpl` textures). The wrapper
//     carries no name of its own; the double extension on disk
//     ("Foo.tpl.dkz") is what tells you what is inside, so decoding just
//     strips the ".dkz" suffix and hands the revealed plain ".tpl" file
//     back to this repo's ordinary file-type detection/decode path.
//
// (2) ".chd" / ".cbd" / ".c3d" -- "CHDp"/"C3Dp" hit-sound bank trio, one
//     triplet per named bank under `files/se/*.chd`+`.cbd`+`.c3d` (e.g.
//     mario, kirby, zelda, banana, quiz, bigband, nes -- ~26-73 banks per
//     disc). All three discs use byte-identical `.chd` header/entry layout
//     (verified against every triplet extracted from all three retail
//     discs: 135 of 138 banks reconstruct their paired `.cbd` exactly to
//     the last byte with no gap/overlap; the other 3 merely alias one
//     header entry's (offset, length) to an earlier entry's -- i.e. two
//     trigger IDs sharing one physical clip, not a layout mismatch). Donkey
//     Konga 2 and 3 ship only the `.chd`+`.cbd` pair, never a `.c3d`
//     sidecar -- `.c3d` is Donkey Konga (disc 1) only.
//
//     ".chd" ("CHDp") layout, all fields big-endian:
//       char magic[4];       // "CHDp"
//       u32  unknown0;       // varies per bank, not decoded
//       u16  unknown1;
//       u16  count;          // number of following 176-byte entries
//       u32  cbd_size;       // exact byte size of the paired .cbd file
//                             // (confirmed exactly against every sample)
//     Followed immediately by `count` fixed 176-byte entries (chd size is
//     exactly 16 + count*176 in every sample):
//       u32  cbd_offset;      // +0x00: byte offset of this clip's raw
//                               // DSP-ADPCM stream inside the paired .cbd
//       u32  unknown2;        // +0x04: varies per entry, not decoded
//       u8   unknown3[72];    // +0x08..+0x4f: a block of mostly-constant
//                               // per-bank float/int fields (pitch/volume-
//                               // looking values) plus a small incrementing
//                               // sound-id word at +0x28; not decoded
//       u32  unknown4;        // +0x50: varies per entry (roughly, but not
//                               // exactly, proportional to nibble_count),
//                               // not decoded
//       u32  nibble_count;    // +0x54: total ADPCM nibble count for this
//                               // clip (confirmed: (nibble_count+1)/2 bytes
//                               // rounded up to a 32-byte boundary exactly
//                               // equals the gap to the next entry's
//                               // cbd_offset in every non-aliased sample)
//       u32  sample_rate;     // +0x58: 22050/24000/32000/44100/48000 seen
//       u32  unknown5;        // +0x5c: 0 in every sample seen
//       u32  unknown6;        // +0x60: small constant (2) in every sample
//                               // seen, not decoded
//       u32  loop_end;        // +0x64: always nibble_count-1, matching the
//                               // same loop_end-with-no-loop convention
//                               // this repo already decodes in lib-ptd.c
//       s16  coef[16];        // +0x68..+0x87: standard GameCube DSP-ADPCM
//                               // coefficient table (verified: small
//                               // magnitude values that reproduce plausible
//                               // ADPCM history coefficients; the .cbd
//                               // payload at cbd_offset itself starts
//                               // directly with an 8-byte ADPCM frame, not
//                               // another copy of the coefficient table)
//       u8   unknown7[40];    // +0x88..+0xaf: mostly-constant tail, not
//                               // decoded
//
//     ".c3d" ("C3Dp") is byte-identical to ".chd" in its 16-byte base
//     header and every 176-byte entry (same field offsets/values,
//     confirmed against every sample), but appends one extra 20-byte
//     record per entry after the whole entry table (c3d size is exactly
//     16 + count*176 + count*20). Every one of these 20-byte records seen
//     is byte-for-byte identical, "00 B4 00 B4 3F 80 00 00 43 FA 00 00
//     40 A0 00 00 43 E1 00 00" -- a constant default 3D-emitter parameter
//     block (min/max distance, volume, rolloff, doppler-looking floats)
//     that never varies with the bank or entry, i.e. a template value
//     rather than per-clip data; not decoded further. Because a `.c3d`
//     bank's audio is otherwise identical to its paired `.chd`, decoding
//     `.c3d` produces the same clips as `.chd`+`.cbd` for that bank.
//
//     ".cbd" has no header of its own: it is simply the concatenation of
//     every entry's raw DSP-ADPCM stream, each starting at its entry's
//     cbd_offset and occupying ceil((nibble_count+1)/2, 32) bytes, reusing
//     this repo's existing DSP-ADPCM decoder (lib-dspadpcm.c/lib-dsp.c) by
//     building a standard-layout in-memory .dsp buffer per clip and
//     decoding that, rather than reimplementing ADPCM decode.
#ifndef SZS_LIB_DONKEYKONGA_H
#define SZS_LIB_DONKEYKONGA_H 1

#include "lib-nintendo.h"

//-----------------------------------------------------------------------------
// (1) ".tpl.dkz" "DKZF" zlib texture wrapper

bool IsDKZF (const u8 *data, size_t size);

// Inflate D's wrapped payload (malloc-owned via *dest). *dest_size receives
// the decompressed size, which is cross-checked against the header field.
enumError DecodeDKZF (const u8 *data, size_t size, u8 **dest, uint *dest_size);

//-----------------------------------------------------------------------------
// (2) ".chd" / ".c3d" "CHDp" / "C3Dp" + ".cbd" hit-sound bank

// True if 'chd_data' is a well formed CHDp or C3Dp header+entry table.
// Only validates the header/table itself, not the paired .cbd.
bool IsDKSoundBank (const u8 *chd_data, size_t chd_size);

// Splits every clip named by 'chd_data's entry table into a WAV file,
// reading each clip's raw ADPCM payload out of 'cbd_data' (the paired
// .cbd, whole file, exact size). Entries whose declared range would
// overrun the .cbd, or whose paired .cbd file size doesn't match the
// header's own cbd_size field, are rejected/skipped defensively rather
// than trusted blindly. Output entries are named "clip###.wav" in header
// order; 'entries' is malloc-owned (see OwnedEntryAdd/ResetOwnedEntries).
enumError ScanDKSoundBank (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *chd_data,
	size_t chd_size, const u8 *cbd_data, size_t cbd_size);

#endif // SZS_LIB_DONKEYKONGA_H
