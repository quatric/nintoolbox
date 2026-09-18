// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 DRP encrypted container (.drp).
//
// Reference: KillzXGaming/Smash-Forge, "Smash Forge/Filetypes/DRP.cs"
// (MIT licensed; clean-room C port).
//
// The payload is obfuscated with a seeded xorshift stream cipher
// (RandomXS: seed = big-endian u32 at 0x1C) chained through the words,
// then holds a file table (count = big-endian u16 at 0x16, records at
// 0x60: 0x40-byte name, unknowns, 4 part sizes) whose parts are raw
// zlib streams (typically OMO animations and NTWD/NUT textures).
// The reference tool ships no re-encryption, so this side is
// extract-only.
#ifndef LIB_SMASHTDRP_H
#define LIB_SMASHTDRP_H 1

#include "lib-nintendo.h"

bool IsSmashDRP (const u8 *data, size_t size, ccp name);
enumError ExtractSmashDRPArchive (ccp arg, ccp basedir, uint depth);

#endif // LIB_SMASHTDRP_H
