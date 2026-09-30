// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// ".wsi" blocked DSP-ADPCM streams (Alone in the Dark and Aladdin Magic
// Racer, Wii; Eden Games / Ubisoft). Layout as documented by vgmstream's
// meta/wsi.c and layout/blocked_wsi.c, re-derived here against retail files:
//
//   0x00 u32 start        offset of the first block (0x20 in every sample)
//   0x04 u32 channels     2 in every sample
//   start: a run of blocks, `channels` of them per set, channel 1 first:
//     0x00 u32 block_size  same for every channel of a set
//     0x04 u32 unknown     1
//     0x08 u32 channel     1-based, alternating 1,2,1,2...
//     0x0c u32 unknown     0
//     0x10 ...             ADPCM frames (8 bytes = 14 samples each)
//   In the first block of each channel the payload opens with the channel's
//   own standard 0x60-byte GameCube DSP header (sample count, nibble count,
//   rate, loop, coefficients, history); the frames follow it.
//
// Each channel is concatenated back into one plain .dsp so the existing
// DSP-ADPCM decoder produces the WAVs. Extract-only.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_WSI_H
#define SZS_LIB_WSI_H 1

#include "lib-nintendo.h"

bool IsWsi (const u8 *data, size_t size);
enumError ScanWsi (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif
