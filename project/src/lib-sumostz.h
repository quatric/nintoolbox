// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Sumo Digital "SSR_Wii" engine tool resource container (.stz; Sonic & Sega
// All-Stars Racing, Wii; Resource/SumoToolResources/*.stz). Reverse-engineered
// from real samples of the retail disc.
//
// All fields big-endian uint32:
//   +0x00 / +0x10 / +0x20: the same self-referential offset, repeated three
//     times (always 0x48 in every sample seen -- likely a locale/variant
//     table collapsed to one entry for a single-language build)
//   +0x28 decompressed_size
//   +0x2c / +0x30 compressed_size (a lower bound: a few bytes short of the
//     real zlib stream length -- just let zlib consume until it reports done)
//   +0x34 / +0x38 small unknown counts (not needed to decompress)
//   +0x3c total file size
//   +0x40 8 bytes, always zero
//   +0x48 raw zlib stream (magic 78 DA); inflates to exactly decompressed_size
//     bytes of an ASCII FourCC-tagged chunk blob (PTEX -- font/UI texture
//     atlas -- in every sample seen). The inner chunk format itself is not
//     decoded; only the outer zlib container is recovered here.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_SUMOSTZ_H
#define SZS_LIB_SUMOSTZ_H 1

#include "lib-std.h"

bool IsSumoSTZ (const u8 *d, size_t size);

// Inflate D's zlib payload (malloc-owned). TAG receives the inner FourCC (4
// bytes, not NUL-terminated) when it looks like plain ASCII, else zeroed.
enumError DecodeSumoSTZ (u8 **dest, uint *dest_size, const u8 *d, size_t size, char tag[4]);

#endif
