// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 interface files: Lumen UI layouts (.lm) and
// texture-atlas tables (texlist).
//
// Reference: KillzXGaming/Smash-Forge, "Smash Forge/Filetypes/UI/LM.cs"
// and "Smash Forge/Filetypes/UI/Texlist.cs" (MIT licensed;
// clean-room C ports of the binary layouts).
#ifndef LIB_SMASHTUI_H
#define LIB_SMASHTUI_H 1

#include "lib-nintendo.h"

bool IsSmashLM (const u8 *data, size_t size, ccp name);
bool IsSmashTexlist (const u8 *data, size_t size);

enumError DecodeSmashLM_Text (FILE *out, const u8 *data, size_t size);
enumError DecodeSmashTexlist_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_SMASHTUI_H
