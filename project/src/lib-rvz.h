#ifndef SZS_LIB_RVZ_H
#define SZS_LIB_RVZ_H 1

#define _GNU_SOURCE 1

#include "lib-std.h"

// Native decoder for Dolphin's WIA/RVZ compressed GameCube/Wii disc image
// format. Spec: https://github.com/dolphin-emu/dolphin/blob/master/docs/WiaAndRvz.md
// (fetched and verified byte-for-byte against real retail .rvz samples --
// see the matching lib-rvz.c comment). Only plain GameCube discs (no Wii
// partition hash-removal reconstruction) are supported so far.

// Checks for the "RVZ\1" or "WIA\1" magic at offset 0.
bool IsRVZ (cvp data, uint size);

// Decodes SRC_PATH (a .rvz/.wia file) to a plain raw ISO/GCM image at
// DEST_PATH. Returns ERR_NOTHING_TO_DO if SRC_PATH isn't RVZ/WIA data,
// ERR_NOT_IMPLEMENTED for the unsupported cases noted above (Wii discs,
// non-Zstandard compression), ERR_OK/ERR_WARNING on success.
enumError DecodeRVZFile (ccp src_path, ccp dest_path);

#endif // SZS_LIB_RVZ_H 1
