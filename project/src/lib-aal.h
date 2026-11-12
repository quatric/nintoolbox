// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_AAL_H
#define SZS_LIB_AAL_H 1

#include "lib-std.h"

// Nintendo's first-party audio library (AAL) data files, converted to text. All are little endian
// ("FFFE" BOM). Each converter returns a MALLOC'd text (free with FREE) or ERR_INVALID_DATA.
//
// AMTA 5.0 (".amta", members of ".bars" archives): audio metadata of one asset.
//   0x08 size, 0x10 data offset, 0x14 marker table, 0x18 MINF music info, 0x1C tag table, 0x20 parameter
//   table (all absolute, 0 = none), 0x24 name (offset relative to the field), 0x28 crc32 of the name,
//   0x2C flags, 0x30 u8 track count, u8 channel count, then one {u8 channel, u8 speaker} pair per channel.
//   Data: u32 mask; bit 0 peak amplitude, bit 1 RMS amplitude (both linear, checked against the decoded
//   wave), bits 2 and 3 two levels in dB, bit 5 a level envelope {u16 count, u16 kind, count x {f32 level,
//   u32 sample}}, bit 6 one more float. Markers {id, name, start, length}, tags {name}, parameters
//   {name, u32 value}; names are offsets relative to their own field.
// AATN 1 (".baatn"): attenuator, five {curve name, curve type} pairs (volume, env fx, filter, spread,
//   priority), directivity and culling names, listener and occlusion flags, string table at 0x08.
// AROC 1/2 (".baroc"): roll-off curve. AACL 1/2 (".baacl"): attenuation culling.
// AADR 1 (".baadr", 0x18 bytes): directivity cone, f32 inner and outer angle (degrees), f32 volume
//   outside the cone, then one more f32 (0.2 / 0.0 in Tomodachi Life: Living the Dream).
// AGST (".bagst"): group settings; the group tree with names, each group's parameter words and the
//   BYML documents of the other sections (MAND, PASE, GCCD, BFIL, ...) decoded inline.
enumError ConvertAMTAToYaml (const u8 *data, size_t size, char **text, size_t *text_size);
enumError ConvertAATNToYaml (const u8 *data, size_t size, char **text, size_t *text_size);
enumError ConvertAROCToYaml (const u8 *data, size_t size, char **text, size_t *text_size);
enumError ConvertAACLToYaml (const u8 *data, size_t size, char **text, size_t *text_size);
enumError ConvertAADRToYaml (const u8 *data, size_t size, char **text, size_t *text_size);
enumError ConvertAGSTToYaml (const u8 *data, size_t size, char **text, size_t *text_size);

// Acoustic probe grid (".dapg", Mario Kart World Sound/Acoustic/Probe/<scene>/<x>_<z>.dapg): u32 grid
// size N (64), N*N u16 cell indices (0 = no probe, else record index + 1), then N*N records of 0x7C bytes:
// f32, f32, u16, u16, four f32, then 12 {f32 distance, u32 tag} rays (three groups of four). Written as
// CSV, one line per cell.
enumError ConvertDAPGToCsv (const u8 *data, size_t size, char **text, size_t *text_size);

// Light probe grid (".dlpg", Mario Kart World Env/<world>_X<x>_Z<z>_<level>.dlpg, 28,125 files): "DLPG", u32 version
// 6, then columns of four probes, each column 4 x 160 bytes + {f32 base y, f32 spacing x3}. A probe is 24 floats
// (lighting coefficients, 8 x RGB), 4 + 4 floats, f32 x/y/z position, u32, 16 bytes of packed data. Written as CSV,
// one line per probe.
enumError ConvertDLPGToCsv (const u8 *data, size_t size, char **text, size_t *text_size);

#endif // SZS_LIB_AAL_H
