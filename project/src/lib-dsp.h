// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Nintendo GameCube/Wii DSP-ADPCM standalone audio stream (.dsp).
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_DSP_H
#define SZS_LIB_DSP_H 1

#include "lib-nintendo.h"
#include <stdio.h>

bool IsDSP (const u8 *data, size_t size);
enumError DecodeDSPToWAV (const u8 *data, size_t size, u8 **wav_out, size_t *wav_size_out);
enumError ExtractDSPAudio (ccp arg, ccp basedir, uint depth, const u8 *data, size_t size);

#endif // SZS_LIB_DSP_H
