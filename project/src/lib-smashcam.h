// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 stage camera/animation files: path.bin stage
// paths, CMR0 camera motion and light.bin lighting animation.
//
// Reference: KillzXGaming/Smash-Forge,
// "Smash Forge/Filetypes/Animation/PATH.cs",
// "Smash Forge/Filetypes/Animation/CMR0.cs" and
// "Smash Forge/Filetypes/Animation/LIGH.cs"
// (MIT licensed; clean-room C ports of the binary layouts).
#ifndef LIB_SMASHCAM_H
#define LIB_SMASHCAM_H 1

#include "lib-nintendo.h"

bool IsSmashPath (const u8 *data, size_t size, ccp name);
bool IsSmashCMR0 (const u8 *data, size_t size);
bool IsSmashLIGH (const u8 *data, size_t size);

enumError DecodeSmashPath_Text (FILE *out, const u8 *data, size_t size);
enumError DecodeSmashCMR0_Text (FILE *out, const u8 *data, size_t size);
enumError DecodeSmashLIGH_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_SMASHCAM_H
