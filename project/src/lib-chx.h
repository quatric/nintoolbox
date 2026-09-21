// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Collision Studios character behaviour/rig data (.chx, "Brave: A Warrior's
// Tale"). Magic "DRHC" (a class name "CHRD", byte-reversed, the same
// convention as the engine's other tagged formats).
//
// The binary framing (a header table of section offsets, then fixed-stride
// "subtype" records holding float parameters -- colour tint, scale --
// followed by a NUL-terminated name, e.g. "DefaultSubtype"/"Glowing"/
// "Arctic") was only partially recovered and is NOT decoded here. What
// makes this format worth decoding at all despite that: the rest of the
// file is effectively source code -- a stream of individually
// NUL-terminated ASCII strings that names every animation state
// (SwimIdle, ACT_SWIM_FAST, ...) and spells out full behaviour-script
// calls verbatim, e.g. "BeginFixedCam(3,1,0,0,0.0,0,0)",
// "SetBehaviourMode(EVADE)", "PlayAudio( FISH_SPLASH, CHAR )". Every one
// of those strings is recovered, in file order, to a text sidecar; the
// binary structure connecting them (which command belongs to which
// animation event) is not.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_CHX_H
#define SZS_LIB_CHX_H 1

#include "lib-std.h"

enumError ExtractCHXArchive (ccp arg, ccp basedir, uint depth);

#endif
