#ifndef SZS_LIB_RSAR_CONV_H
#define SZS_LIB_RSAR_CONV_H 1

#include "lib-std.h"

// Native Wii BRSAR (RSAR) -> standard MIDI + SF2 conversion.
//
// Ported from the Nintendo logic of VGMTrans (magcius / khang06 "siiva" branch):
//   RSARScanner.cpp  - RSAR SYMB/INFO parsing: sound, bank, file and group tables,
//                      which RBNK / RWAR / RSEQ each sound and bank resolves to
//   RSARInstrSet.cpp - RBNK key/velocity region trees, envelope tables, DSP-ADPCM /
//                      PCM8 / PCM16 wave decoding from a WAVE block or an RWAR
//   RSARSeq.cpp      - RSEQ master-track entry points (see SequenceToMIDIFrom)
//   VGMColl / SynthFile / SF2File / ScaleConversion - collection -> SF2 mapping
//
// Output layout:
//   <out_dir>/midi/<sound>.mid      one MIDI per sequence sound
//   <out_dir>/midi/<sound>.wav      the sound rendered by the native NintendoWare engine model
//                                   (lib-nwseq.c), 32 kHz stereo; RSARRenderedHook may turn it into FLAC
//   <out_dir>/midi/sequences.tsv    "midi<TAB>soundfont<TAB>audio" manifest (bank used by each MIDI;
//                                   audio = "native" when the WAV came from the native renderer)
//   <out_dir>/soundfonts/<bank>.sf2 one SoundFont per bank used by at least one sequence
//
// Returns ERR_NOTHING_TO_DO when the data is not an RSAR archive, so callers can fall
// back to another converter for BFSAR / BCSAR / SDAT.
enumError ConvertRSARNative (const u8 *data, size_t size, ccp out_dir);

// Called with the path of every WAV the native sequence renderer wrote (e.g. to encode it to FLAC).
extern void (*RSARRenderedHook) (ccp wav_path);

// When true, ConvertRSARNative() also renders every sequence sound to audio (off by default: walking a
// whole tree must not fill the disk with WAVs).
extern bool RSARRenderAudio;

// Nintendo DS SDAT -> MIDI + SF2, same output layout (port of NDSScanner / NDSInstrSet).
// SSEQ sequences become MIDI through the sequence library; every bank used by a sequence
// becomes a SoundFont holding the eight synthesized PSG duty waves followed by the samples
// of the bank's wave archives.
enumError ConvertSDATNative (const u8 *data, size_t size, ccp out_dir);

// Wii U / Switch (FSAR) and 3DS (CSAR) sound archive -> MIDI + SF2, same output layout. Every FSEQ/CSEQ
// sound becomes a MIDI; every bank (FBNK/CBNK + its FWAR/CWAR waves) used by one becomes a SoundFont.
// Only assets embedded in the archive's FILE pool are converted (external group files are not read).
enumError ConvertFSARNative (const u8 *data, size_t size, ccp out_dir);

// SoundFont for one standalone FBNK/CBNK bank plus a FWAR/CWAR wave archive holding its waves.
enumError ConvertFBNKToSF2 (const u8 *bank, size_t bank_size, const u8 *war, size_t war_size, ccp bank_name,
	u8 **out_sf2, size_t *out_size);

// Builds a SoundFont for one RBNK (instrument bank) plus the wave data it references;
// exposed separately so that tools can convert a standalone bank + wave archive.
//   rbnk  : RBNK file image; wave : RWAR file image (may be NULL for banks with an
//           embedded WAVE block, whose sample data then lives in rbnk itself).
enumError ConvertRBNKToSF2 (const u8 *rbnk, size_t rbnk_size, const u8 *wave, size_t wave_size,
	ccp bank_name, u8 **out_sf2, size_t *out_size);

// NW4R wave data -> WAV: an RWAR archive (every RWAV in it) or a single RWAV. Writes <stem>_NNN.wav
// (16 bit PCM, interleaved channels, loop points as a 'smpl' chunk) into out_dir.
enumError ConvertRWARToWAV (const u8 *data, size_t size, ccp out_dir, ccp stem, uint *n_written);

// Wavetable pair ".wt" + ".pcm" (HOME Button SE bank, Octomania games) -> SoundFont (melodic programs only).
enumError ConvertWTToSF2 (const u8 *wt, size_t wsz, const u8 *pcm, size_t psz, ccp name, u8 **out_sf2, size_t *out_size);

#endif
