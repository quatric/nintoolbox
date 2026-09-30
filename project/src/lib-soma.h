// SPDX-License-Identifier: GPL-2.0+
// Monolith Soft / Procyon Studio 2D graphics and animation formats
// (.obp / .ntp / .bgp / .dad / .pcs; Soma Bringer, Nintendo DS)
#ifndef LIB_SOMA_H
#define LIB_SOMA_H 1

#include "lib-std.h"

// (1) ".obp" / ".ntp" OBP1 sprite/object texture
bool IsSomaObp (const u8 *data, size_t size, size_t file_size);
enumError DecodeSomaObp_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

// (2) ".bgp" BGP1 background graphic
bool IsSomaBgp (const u8 *data, size_t size, size_t file_size);
enumError DecodeSomaBgp_Text (FILE *f, const u8 *data, size_t size, size_t file_size);

// (3) ".dad" Monolith Soft DAD LZSS compressed container
bool IsSomaDad (const u8 *data, size_t size);
enumError DecompressSomaDad (u8 *dst, size_t dst_len, const u8 *src, size_t src_len);
enumError DecompressSomaDad_Alloc (u8 **dst_out, size_t *dst_len_out, const u8 *src, size_t src_len);

// (4) ".pcs" Monolith Soft 2D animation / layout sequence container
bool IsSomaPcs (const u8 *data, size_t size);
enumError DecodeSomaPcs_Text (FILE *f, const u8 *data, size_t size);

#endif // LIB_SOMA_H
