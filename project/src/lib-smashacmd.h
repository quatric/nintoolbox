// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 moveset files: ACMD scripts (game.bin,
// effect.bin, sound.bin, expression.bin) and motion.mtable.
//
// Reference: Sammi-Husky/Sm4sh-Tools, SALT/Scripting/AnimCMD/
// (ACMDFile.cs, ACMDScript.cs, MTable.cs), used by
// KillzXGaming/Smash-Forge (MovesetManager.cs, ACMDScript.cs).
// MIT licensed; clean-room C port of the container layout. The
// per-command opcode/size dictionary lives in SALT's CMD_INFO.cs and
// is not part of the file, so commands are reported structurally
// (offset, word count, opcode, raw words); names are not decoded here.
#ifndef LIB_SMASHACMD_H
#define LIB_SMASHACMD_H 1

#include "lib-nintendo.h"

bool IsSmashACMD (const u8 *data, size_t size);
bool IsSmashMTable (const u8 *data, size_t size, ccp name);

enumError DecodeSmashACMD_Text (FILE *out, const u8 *data, size_t size);
enumError DecodeSmashMTable_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_SMASHACMD_H
