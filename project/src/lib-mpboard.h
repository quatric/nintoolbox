// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_MPBOARD_H
#define SZS_LIB_MPBOARD_H 1

#include "types.h"
#include <stdio.h>

// Mario Party Board Data.
// Supports:
// 1. GC/Wii binary board files (MP4 - MP8) - board space nodes, coordinates, links
// 2. Super Mario Party (SMP) CSV board node formats
// Reference: MPLibrary/GCWii/Board.cs & Switch/SMP/Board.cs & Switch/SMP/SpaceNode.cs.

typedef struct mp_space_node_t
{
	float pos[3];
	float rot[3];
	float scale[3];
	u16 param1;
	u16 param2;
	u16 param3;
	u16 type_id;
	u16 num_links;
	u16 links[16];
} mp_space_node_t;

typedef struct mp_board_t
{
	uint num_spaces;
	mp_space_node_t *spaces;
	uint version; // 4, 5, 6, 7, 8
} mp_board_t;

bool IsMPBoard (const u8 *data, size_t size);

enumError ScanMPBoard (mp_board_t *board, const u8 *data, size_t size, uint version);
void ResetMPBoard (mp_board_t *board);

// Dumps board spaces to human-readable text / JSON / CSV.
enumError DecodeMPBoard_Text (FILE *out, const mp_board_t *board);
enumError DecodeMPBoard_CSV (FILE *out, const mp_board_t *board);

// SMP CSV board parsing
enumError DecodeSMPBoard_Text (FILE *out, const u8 *data, size_t size);

#endif // SZS_LIB_MPBOARD_H
