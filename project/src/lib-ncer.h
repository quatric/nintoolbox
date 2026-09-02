#ifndef LIB_NCER_H
#define LIB_NCER_H

#include "lib-nintendo.h"

// Bounded view of a Nitro NCER cell bank. Object attributes are the original
// six-byte DS OAM records and remain owned by the NCER file buffer.
typedef struct nintendo_ncer_t
{
	const u8 *data, *cells, *objects;
	uint size, n_cells, cell_size, objects_size;
	uint mapping_mode;
} nintendo_ncer_t;

enumError ScanNCER (nintendo_ncer_t *ncer, const u8 *data, uint size);
enumError GetNCERCell (
	const nintendo_ncer_t *ncer, uint index, uint *n_objects, const u8 **oam_records);

// Bounded view of a Nitro NANR animation bank. Frame records and cell indices
// remain pointers into the original input data.
typedef struct nintendo_nanr_t
{
	const u8 *data, *animations, *frames, *frame_data;
	uint size, n_animations, n_frames, frames_size, frame_data_size;
} nintendo_nanr_t;

enumError ScanNANR (nintendo_nanr_t *nanr, const u8 *data, uint size);
enumError GetNANRAnimation (
	const nintendo_nanr_t *nanr, uint index, uint *n_frames, const u8 **frame_records);

#endif
