// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 sound sequence archive (.sqb, magic "SQB\0").
//
// Reference: KillzXGaming/Smash-Forge,
// "Smash Forge/Filetypes/Sounds/SQB.cs"
// (MIT licensed; clean-room C port of the binary layout).
//
// Only sequence structure is handled; no audio-stream decoding.
//
// Smash 4 sound banks (.nus3bank) live in lib-nus3bank.h.
#ifndef LIB_SMASHTSQB_H
#define LIB_SMASHTSQB_H 1

#include "lib-nintendo.h"

bool IsSmashSQB (const u8 *data, size_t size);
enumError DecodeSmashSQB_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_SMASHTSQB_H
