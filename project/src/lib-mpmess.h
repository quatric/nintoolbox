// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_MPMESS_H
#define SZS_LIB_MPMESS_H 1

#include "types.h"
#include "lib-archive-util.h"
#include <stdio.h>

// Mario Party 4-7 / GCN Message files (board.dat, mini.dat, mini_e.dat, board_e.dat).
// Reference: MPLibrary/GCWii/Message/MessFile.cs & MessFileData.cs.

bool IsMPMESS (const u8 *data, size_t size);

// Extracts the message file container into text / JSON / subfile strings.
enumError ScanMPMESS (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

// Dumps decoded message strings to out file stream.
enumError DecodeMPMESS_Text (FILE *out, const u8 *data, size_t size);

#endif // SZS_LIB_MPMESS_H
