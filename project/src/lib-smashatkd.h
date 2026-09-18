// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 attack/subaction frame data (.bin, magic "ATKD").
//
// Reference: KillzXGaming/Smash-Forge, "Smash Forge/Filetypes/ATKD.cs"
// (MIT licensed; clean-room C port of the binary layout).
#ifndef LIB_SMASHATKD_H
#define LIB_SMASHATKD_H 1

#include "lib-nintendo.h"

bool IsSmashATKD (const u8 *data, size_t size);
enumError DecodeSmashATKD_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_SMASHATKD_H
