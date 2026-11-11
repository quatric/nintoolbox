#ifndef SZS_LIB_MP2K_H
#define SZS_LIB_MP2K_H 1

#include "lib-std.h"

// GBA MusicPlayer2000 (Sappy / m4a) ROM -> standard MIDI (port of VGMTrans MP2kScanner + MP2kSeq).
// Writes <out_dir>/song_NNN.mid for every song of the engine's song table.
// Returns ERR_NOTHING_TO_DO when no sound engine is found.
enumError ConvertMP2kROM (const u8 *data, size_t size, ccp out_dir, uint *n_songs);

#endif
