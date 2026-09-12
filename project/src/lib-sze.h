// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_SZE_H
#define SZS_LIB_SZE_H 1

#include "lib-nintendo.h"

// SZE (Encrypted SZS / SARC / Zstd container used by F-Zero 99 and Switch titles).
// Wraps encrypted payload behind a 32-byte header with AES-128-CTR/CBC.
enumError DecodeSZE (
	u8 **dest, uint *dest_size, const u8 *data, uint size, const u8 key[16]);

enumError EncodeSZE (
	u8 **dest, uint *dest_size, const u8 *data, uint size, const u8 key[16], const u8 iv[16], uint mode);

#endif
