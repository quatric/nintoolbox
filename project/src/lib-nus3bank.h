// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 sound bank (.nus3bank, "NUS3" + "BANKTOC").
//
// Reference: KillzXGaming/Smash-Forge,
// "Smash Forge/Filetypes/Sounds/NUS3BANK.cs"
// (MIT licensed; clean-room C port of the binary layout).
//
// Different from the NUS3AUDIO stream archive already handled in
// lib-nus3audio.c despite the shared "NUS3" magic: a bank holds a
// section table (PROP project info, BINF bank name, GRP group names,
// DTON tone descriptors, TONE tone metadata, PACK tone payloads).
// Only structure, metadata and raw tone payloads are handled here;
// no audio-stream decoding is done.
#ifndef LIB_NUS3BANK_H
#define LIB_NUS3BANK_H 1

#include "lib-nintendo.h"

bool IsNUS3Bank (const u8 *data, size_t size);
enumError DecodeNUS3Bank_Text (FILE *out, const u8 *data, size_t size);
enumError ExtractNUS3BankArchive (ccp arg, ccp basedir, uint depth);

#endif // LIB_NUS3BANK_H
