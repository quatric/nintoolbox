// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 stage/level data (.lvd, magic "LVD1").
//
// Reference: KillzXGaming/Smash-Forge, "Smash Forge/Filetypes/LVD.cs"
// (MIT licensed; clean-room C port of the binary layout).
#ifndef LIB_SMASHLVD_H
#define LIB_SMASHLVD_H 1

#include "lib-nintendo.h"

bool IsSmashLVD (const u8 *data, size_t size);
enumError DecodeSmashLVD_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_SMASHLVD_H
