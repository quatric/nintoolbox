// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Collision Studios subtitle cue table (.sbt, "Brave: A Warrior's Tale").
//
// "sbtf" magic, u32 version, u32 cue_count, then a QuickTime-style "CTTS"
// (composition-time-to-sample) sub-atom holding per-cue big-endian float
// timing pairs -- the exact record layout was NOT fully recovered (it does
// not appear to be a fixed stride across samples of different cue counts).
// What IS recovered and stable across every sample checked: a run of
// NUL-terminated ASCII cue names/keys follows the CTTS block and reaches
// exactly to EOF, one string per subtitle cue in order (a mute cue is
// literally the placeholder name "BLANK"). These are keys into whatever
// table holds the actual localized display text, not the text itself.
//
// Decoded to a plain-text sidecar (one cue key per line) rather than
// guessing at the unresolved timing layout.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_SBT_H
#define SZS_LIB_SBT_H 1

#include "lib-std.h"

enumError ExtractSBTArchive (ccp arg, ccp basedir, uint depth);

#endif
