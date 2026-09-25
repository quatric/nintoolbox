// SPDX-License-Identifier: GPL-2.0+
// Tenchu: Shadow Assassins (Wii, Acquire, 2009) "T4-*" tagged resource
// family. No public documentation of this format exists (checked
// Xentax/ZenHax mirrors, GitHub, and Wii/Tenchu modding communities via web
// search -- nothing turned up). Ships as generic ".b" files under
// DATA/files/Common/{Camera,AI}/ alongside several other, unrelated ".b"
// layouts also used in this title (see below) -- ".b" is reused by the
// engine as a catch-all binary extension, it is not one format.
//
// Confirmed against retail samples (Common/Camera/CameraSetDB.b,
// Common/Camera/{ayame,rikimaru}_camset.b, Common/AI/AIVoiceCommon.b,
// Common/AI/AIStatusData.b, Common/AI/CMN_AI_scr.b -- the only 6 real ".b"
// files on the disc that carry this magic):
//   char tag[N] = "T4-" + a human-readable type name (e.g. "T4-CamSetDB",
//                 "T4-AIVoice", "T4-AIScript", "T4-AIStatus", "T4-CamSet"),
//                 NUL-terminated, observed padded to a 12-byte field;
//   u8 date_bcd[4] -- packed BCD, observed {0x20,0x08,0x03,0x14} decoding to
//                 2008-03-14, i.e. an authoring-tool build/export date
//                 (plausible: Havok middleware in the same disc's Motion/
//                 files carries a similar embedded date-like version tag);
//   the remainder is a per-type record table (an entry count followed by
//   fixed-stride records) whose stride and field meanings differ per "T4-"
//   type and were not reverse-engineered here -- e.g. CamSetDB's records
//   are 12 bytes and open with an ASCII character name ("RIKIMARU", ...),
//   while AIVoice's differ. Decoding every "T4-" subtype's record layout
//   individually is future work; this module only confirms the tag, name,
//   and date, common to the whole family.
//
// This is unrelated to the movie-subtitle-cue ".b" files under
// DATA/files/*/Movie/ (magic-less, a u32 count header equal to 2x the
// number of following 16-byte records, each record's last u32 always
// 0xFFFFFFFF) or the plain-count ".b" tables under Common/Event/ -- neither
// of those carry a "T4-" tag and are not handled by this module.

#ifndef SZS_LIB_T4RES_H
#define SZS_LIB_T4RES_H 1

#include "types.h"
#include <stdio.h>

int IsT4Res (const u8 *data, size_t size, size_t file_size);
enumError DecodeT4Res_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // SZS_LIB_T4RES_H
