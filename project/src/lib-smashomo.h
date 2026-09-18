// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 object-motion animation (.omo, magic "OMO ").
//
// Reference: KillzXGaming/Smash-Forge, "Smash Forge/Filetypes/Animation/OMO.cs"
// (MIT licensed; clean-room C port of the binary layout).
#ifndef LIB_SMASHOMO_H
#define LIB_SMASHOMO_H 1

#include "lib-nintendo.h"

bool IsOMO (const u8 *data, size_t size);
enumError DecodeOMO_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_SMASHOMO_H
