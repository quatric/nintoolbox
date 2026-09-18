// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 SALT parameter files (fighter_param.bin,
// stage params, *.bin with a leading 0xFFFF magic).
//
// Reference: Sammi-Husky/Sm4sh-Tools, SALT/Params/ParamFile.cs
// (via KillzXGaming/Smash-Forge's SALT.dll usage), MIT licensed;
// clean-room C port of the documented binary layout.
//
// Header (8 bytes): big-endian u16 0xFFFF + 6 reserved bytes.
// Body: stream of [u8 type][payload] with type codes
//   0x01 s8, 0x02 u8, 0x03 s16BE, 0x04 u16BE, 0x05 s32BE,
//   0x06 u32BE, 0x07 f32BE, 0x08 string (s32BE length + bytes),
//   0x20 group (s32BE entry count, starts a new value group).
// Values before the first group form a flat list; every group holds
// EntryCount entries of equal size.
#ifndef LIB_SALTPARAM_H
#define LIB_SALTPARAM_H 1

#include "lib-nintendo.h"

bool IsSaltParam (const u8 *data, size_t size);
enumError DecodeSaltParam_Text (FILE *out, const u8 *data, size_t size);

#endif // LIB_SALTPARAM_H
