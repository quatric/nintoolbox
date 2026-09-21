// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Sumo Digital "iSWS" standalone audio stream (.was; Sonic & Sega All-Stars
// Racing, Wii; Resource/Audio/wav/**/*.was). Reverse-engineered from real
// samples of the retail disc.
//
// Global header (0x20 bytes, all fields big-endian uint32 unless noted):
//   +0x00 magic "iSWS"
//   +0x04 header size, always 0x18 in every sample seen (not the byte offset
//     of the first per-channel sub-header, which is fixed at 0x20)
//   +0x08 channel count
//   +0x0c block_count - 1 (redundant with ceil(per-channel byte length /
//     block size) below; kept only as a cross-check, not relied on)
//   +0x10 block size in bytes -- always 0x8000 in every sample seen
//   +0x14 per-channel decodable byte length (excludes the zero padding a
//     truncated last block carries out to a full block size)
//   +0x18 / +0x1c always zero
//
// Followed immediately by one per-channel DSP-ADPCM sub-header, 0x60 bytes,
// byte-identical in layout to this repo's standalone .dsp format (see
// lib-dsp.c/.h): nsamples@0, nibble_count@4, srate@8, loop_flag@12, fmt@14,
// coefs@0x1c, hist1@0x40, hist2@0x42.
//
// Sample data starts right after the last sub-header and is laid out
// BRSTM-style: block-interleaved across channels, each block exactly
// block_size bytes per channel (the last block is zero-padded up to
// block_size even though its real content is shorter). Block b, channel c's
// bytes sit at header_end + (b * channel_count + c) * block_size.
//
// Confirmed two independent ways against real retail samples:
//  1. header_end + block_count * channel_count * block_size reconstructs the
//     on-disk file size exactly, for mono and 2-channel files of very
//     different lengths (COM_ENG/COM_POS_D_129.was 32896 bytes/1 block,
//     COM_ENG/COM_ATT_SC_15.was 65664 bytes/2 blocks,
//     MUSIC/MUSIC_JS_ENDING.was 327904 bytes/5 blocks x 2 channels,
//     MUSIC/MUSIC_FF_MAIN_D.was 10289376 bytes/many blocks x 2 channels).
//  2. Actually decoding under this hypothesis and inspecting the resulting
//     PCM: durations match each channel's declared nsamples exactly, RMS/
//     peak levels are non-silent and non-clipping, and sample-to-sample
//     discontinuities >20000 (a strong marker of decoding across a channel
//     desync) occur in well under 0.1% of samples throughout, including at
//     every block boundary -- e.g. MUSIC_JS_ENDING.was (2 ch, 5 blocks):
//     0.0011% of samples flagged; MUSIC_FF_MAIN_D.was (2 ch, many blocks,
//     374s): 0.0004% flagged. The sequential-per-channel hypothesis instead
//     desyncs into the next channel's data at the very first block boundary
//     and was not used.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_SUMOWAS_H
#define SZS_LIB_SUMOWAS_H 1

#include "lib-std.h"

bool IsSumoWAS (const u8 *data, size_t size);

// Decode D (a whole .was file) into a freshly MALLOC()ed little-endian PCM16
// RIFF/WAVE file (mono or interleaved multi-channel).
enumError DecodeSumoWASToWAV (const u8 *data, size_t size, u8 **wav_out, size_t *wav_size_out);

enumError ExtractSumoWASAudio (ccp arg, ccp basedir, uint depth, const u8 *data, size_t size);

#endif
