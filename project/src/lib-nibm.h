// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// "NIBM" streamed-audio wrapper (*.aud; CSI: Hard Evidence, Wii).
//
// The file is a serialized class instance: magic "NIBM", u32 version (2),
// u32 count, then length-prefixed type names ("class AudioData", "struct
// AudioData::Streamed"), a few u32 fields (sample rate 44100/22050 appear)
// and finally the audio payload, which in every sample is a complete Ogg
// Vorbis stream. Nothing but that stream is needed to play the sound, so the
// extractor cuts from the first "OggS" page to the end of the file.
//
// Verified on all 311 .aud files of the CSI disc: the Ogg starts at offset 126
// in each, and walking its pages lands exactly on the end of file every time.
// (The "DAHBWU-AUDPCM" label wszst gives these files is a false positive from
// the raw-DSP .AUD of Destroy All Humans, which has no NIBM header.)
//
// Extract-only.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_NIBM_H
#define SZS_LIB_NIBM_H 1

#include "lib-nintendo.h"

// If 'data' is a NIBM file carrying a whole Ogg stream, sets *ogg_off and
// returns true.
bool IsNibmOgg (const u8 *data, size_t size, size_t *ogg_off);

#endif
