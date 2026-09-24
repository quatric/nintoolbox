// SPDX-License-Identifier: GPL-2.0+
// "The Voice" (Wii karaoke game, at least the USA "I Want You" disc) asset
// script formats. All five formats covered here are already plain text on
// disc; nothing here is a binary reverse-engineering result. What this
// module adds is (a) reliable detection so the file-type dispatcher can
// tell these apart from each other and from generic text, and (b) a
// normalized manifest decode that pulls out the fields actually useful for
// asset-graph work (referenced sub-asset paths, timing, event lists)
// instead of just echoing the file back.
//
// Confirmed by reading every sample of each extension found in
// files/songs/*, files/vocal_coach/*, files/game/*, files/osd/* etc. on the
// retail disc (46-57 files per extension):
//
// (1) ".song" -- XML, xmlns="zoe:Song". Root <Song start="F" end="F"> gives
//     the song's overall time range (seconds, always start="0.001" in every
//     sample seen). Children:
//       <SongConfig><Mode id="SOLO|HEADTOHEAD|DUET|SINGOFF|VOICETRAP|
//         AUDITIONFEVER"><Setup mics="N"><Player part="MAIN|DUET1|DUET2"
//         tags="..."/></Setup></Mode>...</SongConfig>
//         -- one <Mode> per supported game mode, with a mic count and one
//         <Player> per mic. Confirmed to appear in every sample in exactly
//         this Mode-id set and order.
//       <PerformanceEvents><Event start="F" name="NAME" enabled="true|
//         false"/>...</PerformanceEvents>
//         -- timestamped gameplay cue toggles. NAME values observed across
//         all samples: VERSE, BRIDGE, CHORUS, INTRO, OUTRO, RED_BUTTON,
//         LONG, LONG_PAGE_EXCEPTION, HARMONY, IMPROV, POWER, SPOTLIGHT,
//         SONG_MOMENT, STEALTHESHOW. Each name is expected to toggle
//         enabled="true" then later enabled="false" (a duration span), but
//         this is not enforced by the game format itself, so the decoder
//         reports events in file order rather than assuming pairing.
//     NOT understood: whether other top-level XML elements exist in songs
//     not shipped on this disc (only <SongConfig> and <PerformanceEvents>
//     were ever seen here); the exact semantic meaning of each event name
//     to the runtime scoring/animation systems.
//
// (2) ".amc" ("animesh catalogue") -- a Lua-like top-level assignment:
//       animeshcatalogue=
//       {
//         { animesh_scene="path/to/File.tas"; },
//         ...
//       }
//     A flat list of referenced ".tas" animated-mesh scene paths, one per
//     brace-delimited entry. "--" line comments are used freely between
//     entries (Lua comment syntax) and are skipped by the decoder. No other
//     key was ever observed inside an entry besides animesh_scene.
//
// (3) ".ams" ("animesh sequence") -- same top-level shape, key
//     "animeshsequence", each entry a timed placement of an ".tas" scene:
//       { StartTime=F; FileName="path.tas"; PlayMode=N; PlayRate=F;
//         FadeIn=F; FadeOut=F; },
//     PlayMode observed values across all samples: 0, 1, 2 (meaning not
//     documented anywhere on disc; reported raw -- looks like a loop/
//     once/ping-pong style enum going by convention elsewhere in this
//     codebase, but that is a guess, not a confirmed fact, so it is NOT
//     asserted as such here).
//
// (4) ".palcat" ("palette catalogue") -- same top-level shape, key
//     "palettecatalogue", entries reference a ".pal" raw-RGBA palette file:
//       { palette="path.pal"; },
//     Every one of the 47 samples on this disc contains EXACTLY one entry;
//     whether more than one is ever valid is not confirmed (no sample
//     exercises it), so the decoder does not assume a maximum.
//
// (5) ".palseq" ("palette sequence") -- same top-level shape, key
//     "palettesequence", entries are timed palette-catalogue index swaps:
//       { StartTime=F; PaletteIndex=N; FadeIn=F; FadeOut=F; },
//     PaletteIndex is presumably an index into the paired ".palcat" for the
//     same asset (matching base filename in every sample seen, e.g.
//     "stay.palcat" + "stay.palseq"), but the pairing is by convention/
//     filename only -- nothing inside either file cross-references the
//     other by name, so this module does not attempt to resolve it.
//
// All five are genuinely simple, fully-tabled grammars (not free-form
// prose), so each gets a real field-level parse and a normalized manifest
// re-emit below, rather than a byte pass-through.
#ifndef LIB_THEVOICE_H
#define LIB_THEVOICE_H 1

#include "lib-std.h"

//-----------------------------------------------------------------------------
// Detection. Each takes the usual (data,size,file_size) probe triple; a
// short FILETYPE probe buffer is accepted as long as it covers the
// distinguishing header token (checked first few hundred bytes only).
// These are plain text formats with no magic bytes, so detection is by
// looking for the format's fixed top-level assignment keyword (or, for
// ".song", the "<Song" root element + "zoe:Song" namespace).

int IsVoiceSong   (const u8 *data, size_t size, size_t file_size);
int IsVoiceAmc    (const u8 *data, size_t size, size_t file_size);
int IsVoiceAms    (const u8 *data, size_t size, size_t file_size);
int IsVoicePalcat (const u8 *data, size_t size, size_t file_size);
int IsVoicePalseq (const u8 *data, size_t size, size_t file_size);

//-----------------------------------------------------------------------------
// Decode to a normalized text manifest. Each parses the source grammar
// (rather than copying it) and re-emits the extracted fields in a fixed,
// readable layout.

enumError DecodeVoiceSong_Text   (FILE *f, const u8 *data, size_t size, size_t file_size);
enumError DecodeVoiceAmc_Text    (FILE *f, const u8 *data, size_t size, size_t file_size);
enumError DecodeVoiceAms_Text    (FILE *f, const u8 *data, size_t size, size_t file_size);
enumError DecodeVoicePalcat_Text (FILE *f, const u8 *data, size_t size, size_t file_size);
enumError DecodeVoicePalseq_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

#endif // LIB_THEVOICE_H
