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
// Output layout (identical to the vgmtrans CLI wbrsar used to call):
//   <out_dir>/midi/<sound>.mid      one MIDI per sequence sound
//   <out_dir>/midi/sequences.tsv    "midi<TAB>soundfont" manifest (bank used by each MIDI)
//   <out_dir>/soundfonts/<bank>.sf2 one SoundFont per bank used by at least one sequence
//
// Returns ERR_NOTHING_TO_DO when the data is not an RSAR archive, so callers can fall
// back to another converter for BFSAR / BCSAR / SDAT.
enumError ConvertRSARNative (const u8 *data, size_t size, ccp out_dir);

// Builds a SoundFont for one RBNK (instrument bank) plus the wave data it references;
// exposed separately so that tools can convert a standalone bank + wave archive.
//   rbnk  : RBNK file image; wave : RWAR file image (may be NULL for banks with an
//           embedded WAVE block, whose sample data then lives in rbnk itself).
enumError ConvertRBNKToSF2 (const u8 *rbnk, size_t rbnk_size, const u8 *wave, size_t wave_size,
	ccp bank_name, u8 **out_sf2, size_t *out_size);

#endif
