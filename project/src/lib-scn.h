/***************************************************************************
 *                         _______ _______ _______                         *
 *                        |  ___  |____   |  ___  |                        *
 *                        | |   |_|    / /| |   |_|                        *
 *                        | |_____    / / | |_____                         *
 *                        |_____  |  / /  |_____  |                        *
 *                         _    | | / /    _    | |                        *
 *                        | |___| |/ /____| |___| |                        *
 *                        |_______|_______|_______|                        *
 *                                                                         *
 *                            Wiimms SZS Tools                             *
 *                          https://szs.wiimm.de/                          *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This file is part of the SZS project.                                 *
 *   Visit https://szs.wiimm.de/ for project details and sources.          *
 *                                                                         *
 ***************************************************************************/

// SCN0: Nintendo NW4R "scene animation" BRRES sub-file.
// Added by this fork.
//
// A SCN0 animates the scene itself rather than a model: light sets, ambient
// lights, lights, fog and cameras. Unlike the other NW4R animations it uses a
// *nested* resource group -- one outer group naming up to five sections, each
// holding its own group of nodes.
//
// Layout reverse-engineered from BrawlLib (BrawlLib/SSBB/Types/SCN0.cs and
// BrawlLib/SSBB/ResourceNodes/SCN0/*) and then verified byte-for-byte against
// 19 retail SCN0 animations, whose every byte below the declared size is
// accounted for by this model.
//
// Two layout rules were found in the retail files and are NOT documented by
// BrawlLib:
//
//  - Animated slots are written in flag-BIT numeric order, not in struct field
//    order. For a camera that puts perspFovY (bit 0x80) physically ahead of
//    rotX (bit 0x2000), even though rotX comes first in the struct.
//  - The trailing string pool is laid out in ordinal name order, and for
//    version 4 the declared file size *includes* it while version 5 stops at
//    the end of the data section.

#ifndef SZS_LIB_SCN_H
#define SZS_LIB_SCN_H 1

#include "lib-std.h"

///////////////////////////////////////////////////////////////////////////////

#define SCN0_MIN_VERSION 4
#define SCN0_MAX_VERSION 5
#define SCN0_DEFAULT_VERSION 5

#define SCN0_MAX_NODE_SIZE 0x5c

//-----------------------------------------------------------------------------
// [[scn0_sect_t]]

typedef enum scn0_sect_t
{
	SCN0_LIGHTSET,
	SCN0_AMBLIGHT,
	SCN0_LIGHT,
	SCN0_FOG,
	SCN0_CAMERA,
	SCN0_N_SECT

} scn0_sect_t;

// the NW4R group names, indexed by scn0_sect_t
extern const char *const scn0_sect_name[SCN0_N_SECT];

// the fixed node struct size of each section, indexed by scn0_sect_t
extern const u8 scn0_node_size[SCN0_N_SECT];

///////////////////////////////////////////////////////////////////////////////
// [[scn0_blob_t]]

// One animated slot of a node. 'slot_off' is the byte offset of the s32 slot
// inside the node struct; when the slot's flag bit is clear the slot holds a
// self relative offset to the payload described here, and when it is set the
// slot holds a fixed value instead and no blob exists.

typedef struct scn0_blob_t
{
	u16 slot_off; // offset of the slot word inside the node struct
	char kind; // 'k'=keyframe set, 'c'=colour array, 'v'=visibility bits
	u8 *data; // alloced payload, stored verbatim
	uint size;

} scn0_blob_t;

///////////////////////////////////////////////////////////////////////////////
// [[scn0_node_t]]

typedef struct scn0_node_t
{
	ccp name; // alloced node name
	u8 raw[SCN0_MAX_NODE_SIZE]; // the node struct, verbatim

	scn0_blob_t *blob; // alloced, in flag-bit order
	uint n_blob;

} scn0_node_t;

///////////////////////////////////////////////////////////////////////////////
// [[scn0_t]]

typedef struct scn0_t
{
	ccp fname; // alloced filename of loaded file
	FileAttrib_t fatt; // file attribute

	uint version; // 4 or 5
	ccp name; // alloced resource name, or NULL
	ccp orig_path; // alloced original source path, or NULL
	uint n_frames;
	uint spec_light; // specular light count, preserved verbatim
	u32 loop;

	scn0_node_t *node[SCN0_N_SECT]; // alloced per section
	uint n_node[SCN0_N_SECT];

} scn0_t;

///////////////////////////////////////////////////////////////////////////////

void InitializeSCN0 (scn0_t *scn);
void ResetSCN0 (scn0_t *scn);

scn0_node_t *AppendNodeSCN0 (scn0_t *scn, scn0_sect_t sect, ccp name);

//-----------------------------------------------------------------------------

enumError ScanRawSCN0 (scn0_t *scn, // SCN0 data structure
	bool init_scn, // true: initialize 'scn' first
	const void *data, // data to scan
	uint data_size // size of 'data'
);

//-----------------------------------------------------------------------------

enumError ScanTextSCN0 (scn0_t *scn, // SCN0 data structure
	bool init_scn, // true: initialize 'scn' first
	ccp src_fname // name of the text source to load
);

///////////////////////////////////////////////////////////////////////////////

enumError SaveRawSCN0 (scn0_t *scn, // pointer to valid SCN0
	ccp fname, // filename of destination
	bool set_time // true: set time stamps
);

//-----------------------------------------------------------------------------

enumError SaveTextSCN0 (scn0_t *scn, // pointer to valid SCN0
	ccp fname, // filename of destination
	bool set_time // true: set time stamps
);

///////////////////////////////////////////////////////////////////////////////

#endif // SZS_LIB_SCN_H
