// SPDX-License-Identifier: GPL-2.0+
// Tenchu: Shadow Assassins (Wii, Acquire, 2009) voice-line manifest (".hd",
// DATA/files/{FR,SP,US}/Sound/<CHARACTER>.hd -- one per character/category,
// 102 real samples on the retail disc, e.g. AYAME.hd, RIKIMARU.hd,
// COMMON.hd, MENU.hd, TUTORIAL.hd). No public documentation of this format
// exists (checked Xentax/ZenHax mirrors, GitHub, Wii/Tenchu modding
// communities via web search -- nothing turned up). Magic-less, but the
// generic ".hd" extension is used for nothing else on this disc (all 102
// real ".hd" files match this exact structure with zero leftover bytes),
// so extension + structural walk is a safe detector.
//
// Confirmed against all 102 real samples (100% exact match, zero leftover
// bytes in every file):
//   u32 total_slots;  // fixed per-character line-ID address space
//   u32 group;        // 0..3 observed; constant per file, meaning not
//                      // pinned down (candidate: voice bus/mixer group)
//   struct { u16 category; u16 variant; s32 index; } slot[total_slots];
//     -- one slot per possible voice-line ID. "index" is either the
//     sequential position (0..n_real-1, strictly increasing, and always
//     equal to that real entry's position among the reals seen so far --
//     i.e. it's redundant with slot order, not an independent lookup) of
//     that line's record in the table below, or the sentinel -1
//     (0xFFFFFFFF) marking an ID in this character's address space that
//     isn't recorded in this particular localization (retail FR/SP/US
//     samples routinely have fewer real slots than total_slots -- e.g.
//     AYAME.hd: 200 real / 542 total -- consistent with per-language
//     voice-line coverage gaps). "category"/"variant" tag what kind of
//     line and which take/variant it is; only 0/1/3 appear as real-entry
//     categories in the checked samples, 2/4 appear only on padding-only
//     (never-recorded) slots.
//   struct { // one per real slot, in slot order == "index" order
//     u32 self_index;  // == this record's own position (redundant)
//     u32 volume;      // 0..127 observed (MIDI-style scale); 127 dominant
//     u32 zero;        // always 0 in every sample checked
//     f32 pitch;       // always 1.0 (0x3F800000) in every sample checked
//     u32 zero2;       // always 0
//     u32 group2;      // constant 77 in nearly every file (117 in
//                       // SEKISHO.hd, 127 in TUTORIAL.hd) -- meaning not
//                       // pinned down, candidate: sound-bank/bus id
//     u32 priority;    // 5/10/50 observed -- candidate playback priority
//     u32 unknown7;    // varies (0/15/20/30/35/40/50/60/70/80/90/100)
//     u32 unknown8;    // varies (0..16 observed) -- candidate delay/pan
//     u32 flag9;       // always 1 in every sample checked
//     u32 flag10;      // 0 or 1
//   } param[n_real];
// n_real (the number of "index != -1" slots in the table above) always
// equals exactly (file_size - 8 - total_slots*8) / 44 with zero remainder,
// in all 102 real samples -- this is the strongest structural confirmation
// available without an in-game reference to check field semantics against.
//
// This module decodes the full structure (header, every slot, every
// parameter record); several individual field *meanings* above are
// inferred from value ranges/constancy rather than proven.

#ifndef SZS_LIB_HDVOICE_H
#define SZS_LIB_HDVOICE_H 1

#include "types.h"
#include <stdio.h>

int IsHdVoice (const u8 *data, size_t size, size_t file_size);
enumError DecodeHdVoice_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // SZS_LIB_HDVOICE_H
