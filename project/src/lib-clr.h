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

#ifndef SZS_LIB_CLR0_H
#define SZS_LIB_CLR0_H 1

#include "lib-std.h"

#define CLR0_MIN_VERSION 3
#define CLR0_MAX_VERSION 4
#define CLR0_DEFAULT_VERSION 4

typedef struct clr0_mat_entry_t
{
	uint target; // 0..10
	u32 color_mask; // ARGB
	bool is_constant;
	u32 solid_color; // ARGB
	u32 *colors; // alloced, n_frames elements
} clr0_mat_entry_t;

typedef struct clr0_mat_t
{
	ccp name; // alloced material name
	clr0_mat_entry_t *entry;
	uint n_entry;
	uint n_entry_alloced;
} clr0_mat_t;

typedef struct clr0_t
{
	ccp fname; // alloced filename of loaded file
	FileAttrib_t fatt; // file attribute

	uint version; // 3 or 4
	ccp name; // alloced resource name of the animation, or NULL
	ccp orig_path; // alloced original source path, or NULL
	uint n_frames; // number of frames
	bool loop; // true: animation loops

	clr0_mat_t *mat; // list of materials, alloced
	uint n_mat; // number of used elements of 'mat'
	uint n_mat_alloced; // number of alloced elements of 'mat'

} clr0_t;

void InitializeCLR0 (clr0_t *clr);
void ResetCLR0 (clr0_t *clr);

clr0_mat_t *AppendMatCLR0 (clr0_t *clr, ccp name);
clr0_mat_entry_t *AppendEntryCLR0 (clr0_mat_t *mat, uint target);

enumError ScanRawCLR0 (clr0_t *clr, bool init_clr, const void *data, uint data_size);
enumError ScanTextCLR0 (clr0_t *clr, bool init_clr, ccp src_fname);
enumError SaveRawCLR0 (clr0_t *clr, ccp fname, bool set_time);
enumError SaveTextCLR0 (clr0_t *clr, ccp fname, bool set_time);

#endif // SZS_LIB_CLR0_H
