#ifndef SZS_LIB_DSPADPCM_H
#define SZS_LIB_DSPADPCM_H 1

#include "lib-std.h"

// Nintendo GameCube/Wii/3DS DSP-ADPCM (the codec ffmpeg calls adpcm_thp).
//
// This is a direct port of the real, working implementation vendored in
// the sibling 'mobipeg' repo's FFmpeg fork (libavformat/dsp_adpcm.c/.h,
// libavcodec/adpcm.c's ADPCM_THP decode case, and libavcodec/adpcmenc.c's
// thp_correlate_coefs()/adpcm_thp_encode_block()), Copyright (c) 2001-2003
// The FFmpeg project and Paul B Mahol, LGPL v2.1+. It is not a from-scratch
// reimplementation: every constant and recurrence below traces back to that
// source. See lib-brstm.h for the container this codec is embedded in.

// Samples carried by one 8-byte DSP-ADPCM frame.
#define DSP_ADPCM_SAMPLES_PER_FRAME 14
// Size of one DSP-ADPCM frame: a predictor/scale byte plus 14 nibbles.
#define DSP_ADPCM_BYTES_PER_FRAME 8
// Practical channel cap for stack-sized coefficient tables (BRSTM's own
// channel count field is a byte, but no real title exceeds a handful).
#define DSP_ADPCM_MAX_CHANNELS 16

// Frames/bytes/nibbles needed to hold 'samples' samples of one channel.
s64 DspAdpcmFrameCount (s64 samples);
s64 DspAdpcmByteCount (s64 samples);
s64 DspAdpcmNibbleCount (s64 samples);

// Nibble address at which 'sample' starts, and its inverse (for loop points).
s64 DspAdpcmNibbleAddress (s64 sample);
s64 DspAdpcmNibblesToSamples (s64 nibbles);

// Run 'nb_frames' frames through the decoder purely for their effect on
// hist1/hist2 -- used to fill in a block's seek-table entry / a loop point's
// history without materializing the decoded samples.
void DspAdpcmAdvance (const u8 *src, s64 nb_frames, const s16 *coefs, s16 *hist1, s16 *hist2);

// Derive the 8 predictor coefficient pairs (Q11, 16 s16 values) for one
// channel from its whole-stream PCM16 history via autocorrelation, the way
// Nintendo's own DSPADPCM tool does. coefsOut must hold 16 s16.
void DspAdpcmCorrelateCoefs (const s16 *source, s64 samples, s16 *coefsOut);

// Encode up to DSP_ADPCM_SAMPLES_PER_FRAME samples into one 8-byte frame,
// picking the coefficient-pair index and exponent that minimize error.
// hist1/hist2 are updated to the reconstructed (not source) last two samples,
// matching what the decoder will have after decoding this frame.
void DspAdpcmEncodeBlock (
	const s16 *samples, int count, u8 *dst, const s16 *coefs, int *hist1, int *hist2);

// Decode one 8-byte frame (up to DSP_ADPCM_SAMPLES_PER_FRAME samples).
void DspAdpcmDecodeBlock (
	const u8 *src, int count, s16 *samples, const s16 *coefs, int *hist1, int *hist2);

#endif // SZS_LIB_DSPADPCM_H
