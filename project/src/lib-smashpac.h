// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 animation container (.pac, magic "PACK").
//
// Reference: KillzXGaming/Smash-Forge, "Smash Forge/Filetypes/PAC.cs"
// (MIT licensed; clean-room C port of the binary layout).
//
// Distinct from the Nd Cube "PAC\0", HAL "ARC\0" and Mario Kart Arcade
// containers already handled elsewhere: Smash fighter PAC files bundle
// .omo skeletal animations and .mta material animations. Little-endian
// files start with "PACK", big-endian ones with "KCAP". Header:
// magic[4], u32 reserved, s32 count, u32 reserved, then three tables
// of count u32s (name offsets, data offsets, sizes), the NUL-terminated
// names, and the member payloads (16-byte aligned on rebuild).
#ifndef LIB_SMASHPAC_H
#define LIB_SMASHPAC_H 1

#include "lib-nintendo.h"

bool IsSmashPac (const u8 *data, size_t size);

enumError ScanSmashPac (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

enumError CreateSmashPac (u8 **out, uint *out_size, const nintendo_sarc_entry_t *entries,
	uint n_entries, bool big_endian);

enumError ExtractSmashPacArchive (ccp arg, ccp basedir, uint depth);
enumError create_smashpac_dir (ccp source, ccp dest);

#endif // LIB_SMASHPAC_H
