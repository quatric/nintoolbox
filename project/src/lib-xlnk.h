// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_XLNK_H
#define SZS_LIB_XLNK_H 1

#include "lib-std.h"

// XLink2 effect / sound link databases (".belnk" ELink, ".bslnk" SLink, magic "XLNK").
//
// Header: magic, u32 data size, u32 version, u32 numResParam, numResAssetParam,
// numResTriggerOverwriteParam, ptr triggerOverwriteParamTablePos, ptr localPropertyNameRefTablePos,
// u32 numLocalPropertyNameRef, numLocalPropertyEnumNameRef, numDirectValue, numRandom, numCurve,
// numCurvePoint, ptr exRegionPos, u32 numUser, ptr conditionTablePos, ptr nameTablePos; then
// numUser sorted u32 user-name CRC32s and numUser absolute user offsets, then the param define
// table (header, {name, type, default} records, own string pool), the asset param records (u64 mask
// + packed refs). "ptr" is u32 on 32-bit layouts and u64 (8-aligned) on 64-bit ones.
//
// Layout families (by version, ELink / SLink):
//   0x1e / 0x1c  BotW, MK8D, ARMS, Splatoon 2, ...           32-bit
//   0x1f / 0x1d  Animal Crossing: New Horizons               32-bit, packed switch conditions
//   0x22 / 0x1f  Splatoon 3, Super Mario 3D All-Stars, ...  64-bit, byte container types, grid
//   0x24 / 0x21+ TotK, Wonder, Tomodachi Life: LtD, ...     64-bit, s16 local property count, jump
// Versions 0x1f and 0x22 occur in two families; each candidate is tried and the one that parses
// cleanly is used (the file extension picks the first try). Wii U files are big endian.
//
// Each user (hashed name, resolved against the name table) holds local properties, user params,
// asset call tables (assets with resolved params, or containers: switch / random / blend / sequence
// / grid / jump over a child range, each child with its condition), action slots -> actions ->
// triggers, property triggers and always triggers. Params are packed {u8 reference type, u24
// value}: direct value table, name table string, curve, random range, arrange groups (ex region) or
// bitfield.

bool IsXlnk (const u8 *data, size_t size);

// HINT is the file name (or 0); ".bslnk" / ".belnk" pick the first family to try.
enumError ConvertXlnkToYaml (const u8 *data, size_t size, ccp hint, char **text, size_t *text_size);

#endif // SZS_LIB_XLNK_H
