// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Collision Studios cutscene "sting" camera-track data (.csi, "Brave: A
// Warrior's Tale"). Magic 0x3033430b (constant across every sample; not a
// text tag under the engine's usual byte-reversed-extension convention).
//
// The header holds a run of six section byte-counts (0x04, 0x0c, 0x14,
// 0x18, 0x1c, 0x20) describing what is almost certainly several
// keyframe/event tracks packed back to back, plus a filesize anchor at
// 0x24 -- verified equal to the file's own size on all 138 samples on the
// disc, with zero exceptions. That anchor is this decoder's validation
// gate. The individual track boundaries implied by the six size fields
// were NOT resolved with confidence (unlike lib-mapbad.c's records, no
// per-record sentinel was found to check them against), so the track
// data itself is carved out untouched as track_data.bin rather than
// sliced by guesswork.
//
// What IS recovered cleanly: a NUL-terminated ASCII string at the fixed
// offset 0xac holds the name of the ".cut" project file this cutscene
// was authored from in the (presumably Maya/3ds Max) tool that exported
// it -- often materially different from and more descriptive than the
// shipped .csi filename (e.g. STL0_TV1.csi's source name is
// "STING1eagleTot.cut"). Verified printable, NUL-terminated and present
// at that exact offset on all 138 samples.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_CSCSI_H
#define SZS_LIB_CSCSI_H 1

#include "lib-std.h"

enumError ExtractCSCsiArchive (ccp arg, ccp basedir, uint depth);

#endif
