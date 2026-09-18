// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 skeleton family: VBN boneset, SB swing bones,
// JTB joint table and MOI model index.
//
// Reference: KillzXGaming/Smash-Forge, "Smash Forge/Filetypes/Models/"
// (VBN.cs incl. class SB, JTB.cs, MOI.cs). C# implementation is MIT
// licensed; this is a clean-room C port of the binary layouts.
#ifndef LIB_SMASHVBN_H
#define LIB_SMASHVBN_H 1

#include "lib-nintendo.h"

// --- detection (magic + structural validation) ---

bool IsVBN (const u8 *data, size_t size);
bool IsSmashSB (const u8 *data, size_t size);
bool IsSmashJTB (const u8 *data, size_t size);
bool IsSmashMOI (const u8 *data, size_t size);

// --- human-readable text manifests (written to an open FILE*) ---

enumError DecodeVBN_Text (FILE *out, const u8 *data, size_t size);
enumError DecodeSmashSB_Text (FILE *out, const u8 *data, size_t size);
enumError DecodeSmashJTB_Text (FILE *out, const u8 *data, size_t size);
enumError DecodeSmashMOI_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_SMASHVBN_H
