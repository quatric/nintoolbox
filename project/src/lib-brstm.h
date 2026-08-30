#ifndef SZS_LIB_BRSTM_H
#define SZS_LIB_BRSTM_H 1

#include "lib-std.h"
#include "lib-dspadpcm.h"

// BRSTM/BFSTM/BCSTM -- Nintendo's streamed-audio containers: BRSTM ("RSTM",
// Wii, big-endian HEAD/ADPC/DATA layout), BFSTM ("FSTM", Wii U, big-endian
// section-table INFO/SEEK/DATA layout), BCSTM ("CSTM", 3DS, same layout as
// BFSTM but little-endian by default).
//
// Field layout ported from mobipeg's FFmpeg fork:
//   libavformat/brstm.c    (RSTM/FSTM demuxer, read_header/read_packet)
//   libavformat/brstmenc.c (brstm_write_rstm / brstm_write_fstm)
// Copyright (c) 2012 Paul B Mahol, LGPL v2.1+.
//
// EncodeBRSTM() always emits a single data block covering the whole stream
// (block_count == 1). That is spec-valid -- nothing in the format requires
// multiple blocks -- but it means large files are not chunked the way a
// real Nintendo encoder would chunk them, and the seek table it writes is
// trivial (one all-zero entry per channel, since block 0's history is
// always zero). DecodeBRSTM() is written to also handle real multi-block
// files: it walks the block table and concatenates each channel's bytes
// across blocks before decoding, ignoring the seek table (a linear decode
// never needs it -- it only matters for seeking mid-stream).
//
// FSTM/CSTM's own INFO/SEEK/DATA field offsets (h1rel/h3rel/ci0/ai0, the
// 46-byte per-channel record, the section table's 0x4000/0x4001/0x4002
// flags) are taken verbatim from brstm_write_fstm(); DecodeBRSTM() reads
// them back structurally the same way it does for RSTM, rather than
// replicating ffmpeg's more roundabout seek sequence -- both resolve to
// the same bytes for a file following the documented ref-table convention.

typedef enum brstm_variant_t
{
	BRSTM_VARIANT_RSTM, // Wii,   "RSTM", always big-endian
	BRSTM_VARIANT_FSTM, // Wii U, "FSTM", big-endian
	BRSTM_VARIANT_CSTM, // 3DS,   "CSTM", little-endian by default
} brstm_variant_t;

typedef struct brstm_audio_t
{
	brstm_variant_t variant;
	int channels;
	int sample_rate;
	s64 n_samples;
	bool is_adpcm; // true: ADPCM_THP: false: PCM16
	bool loop;
	s64 loop_start; // samples
	s16 *pcm[DSP_ADPCM_MAX_CHANNELS]; // decoded / to-encode samples, per channel
} brstm_audio_t;

// Encode 'audio' (audio->pcm[ch] must hold audio->n_samples samples each,
// audio->variant selects the container) into a binary. If 'use_adpcm' is
// set, coefficients are derived from the PCM via DspAdpcmCorrelateCoefs()
// and the stream is ADPCM_THP-encoded; otherwise it is written as raw PCM16.
enumError EncodeBRSTM (u8 **out_data, size_t *out_size, const brstm_audio_t *audio, bool use_adpcm);

// Decode a BRSTM/BFSTM/BCSTM binary to PCM16, auto-detecting the variant
// from the magic/BOM (sets audio->variant). audio->pcm[] is allocated by
// this function (FREE each channel + zero the struct when done).
enumError DecodeBRSTM (brstm_audio_t *audio, const u8 *data, size_t size);

void FreeBRSTMAudio (brstm_audio_t *audio);

#endif // SZS_LIB_BRSTM_H
