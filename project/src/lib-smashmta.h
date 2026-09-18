// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 material animation (.mta, magic "MTA4").
//
// Reference: KillzXGaming/Smash-Forge, "Smash Forge/Filetypes/Animation/MTA.cs"
// (MIT licensed; clean-room C port of the binary layout).
#ifndef LIB_SMASHMTA_H
#define LIB_SMASHMTA_H 1

#include "lib-nintendo.h"

bool IsMTA (const u8 *data, size_t size);
enumError DecodeMTA_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_SMASHMTA_H
