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

// GC/Wii board encode: inverse of ScanMPBoard. Writes BE floats/ints for
// every space + link table. Reference: MPLibrary/GCWii/Board.cs Write().
enumError CreateMPBoard (u8 **dest, uint *dest_size, const mp_board_t *board);

// Super Mario Party (Switch) board spaces.
// Reference: MPLibrary/Switch/SMP/Board.cs + SpaceNode.cs. The CSV is
// Shift-JIS (code page 932); the first line is a header and is skipped.
// Each data line is: ID,child0,child1,child2,child3,Type,Attr1,Attr2
// (children empty when unlinked; only ID/Type/Attrs are parsed -- Position/
// Rotation/Scale are never populated by the reference constructor either).
typedef struct smp_space_t
{
	char id[64];
	u16 links[4];
	uint num_links;
	char type[64];
	char attr1[64];
	char attr2[64];
} smp_space_t;

typedef struct smp_board_t
{
	smp_space_t *spaces;
	uint num_spaces;
} smp_board_t;

bool IsSMPBoard (const u8 *data, size_t size);
enumError ScanSMPBoard (smp_board_t *board, const u8 *data, size_t size);
void ResetSMPBoard (smp_board_t *board);
enumError DecodeSMPBoard_CSV (FILE *out, const smp_board_t *board);
enumError CreateSMPBoard (u8 **dest, uint *dest_size, const smp_board_t *board);

// Mario Party 10 (Wii U) board XML (MasuData list).
// Reference: MPLibrary/WiiU/Board.cs MP10BoardParams/MasuData. The XML itself
// is the text payload of an XB-decoded board file (see lib-xb.c); these
// helpers parse that XML text into typed records and back, so board graphs
// survive as CSV as well as raw XML.
typedef struct mp10_masu_t
{
	int id;
	int area;
	char name[128];
	char type[128];
	int param;
	int uncountble;
	int oneway;
	int jumpstart;
	int jumpend;
	int punish;
	int next[16];
	uint num_next;
	int prev[16];
	uint num_prev;
	float pos[3];
	float quat[4];
} mp10_masu_t;

typedef struct mp10_board_t
{
	char xmlfile[256];
	float version;
	mp10_masu_t *masu;
	uint num_masu;
} mp10_board_t;

bool IsMP10Board (const u8 *data, size_t size);
enumError ScanMP10Board (mp10_board_t *board, const u8 *data, size_t size);
void ResetMP10Board (mp10_board_t *board);
enumError DecodeMP10Board_Text (FILE *out, const mp10_board_t *board);
enumError DecodeMP10Board_CSV (FILE *out, const mp10_board_t *board);
enumError CreateMP10Board (u8 **dest, uint *dest_size, const mp10_board_t *board);

#endif // SZS_LIB_MPBOARD_H
