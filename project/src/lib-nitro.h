#ifndef SZS_LIB_NITRO_H
#define SZS_LIB_NITRO_H 1

#include "types.h"
#include "lib-nintendo.h"

// Nintendo DS ("Nitro") sprite compositing.
//
// A DS sprite is spread across four files: NCGR holds the 8x8 tile pixels,
// NCLR the palettes, NCER the cells (each a list of hardware OAM records
// placing tiles on screen), and NANR the animations (sequences of cells).
// Rendering one needs all of them together, which is what this layer does --
// the individual decoders in lib-nintendo.c only ever see one file.

// Raw view of NCGR tile pixels (indices, not colours).
typedef struct nitro_ncgr_t
{
	const u8 *tiles; // tile pixel data
	uint tiles_size;
	uint n_tiles;
	uint bpp; // 4 or 8
	bool is_1d; // true: 1D linear mapping, false: 2D sheet mapping
	uint mapping_shift; // 0: 32K, 1: 64K, 2: 128K, 3: 256K
	uint tiles_x; // sheet width in tiles (for 2D mapping)
} nitro_ncgr_t;

// Palette entries as RGBA8, 16 colours per palette bank for 4bpp.
typedef struct nitro_nclr_t
{
	u8 *rgba; // 4 bytes per entry (owned)
	uint n_entries;
} nitro_nclr_t;

// Screen tilemap entries for background layouts.
typedef struct nitro_nscr_t
{
	const u8 *data;
	uint data_size;
	uint width; // in pixels
	uint height; // in pixels
	uint color_mode; // 0: 4bpp (16-color), 1: 8bpp (256-color)
	uint bg_type; // 0: Text, 1: Affine, 2: Extended
} nitro_nscr_t;

enumError ScanNitroNCGR (nitro_ncgr_t *ncgr, const u8 *data, uint size);
enumError ScanNitroNCLR (nitro_nclr_t *nclr, const u8 *data, uint size);
void ResetNitroNCLR (nitro_nclr_t *nclr);
enumError ScanNitroNSCR (nitro_nscr_t *nscr, const u8 *data, uint size);

// Renders one NCER cell into a tightly packed RGBA8 image. The cell's OAM
// records use signed screen coordinates, so the output is the bounding box
// of every object in the cell; *ox/*oy receive the box origin so callers can
// line up multiple cells consistently.
enumError RenderNCERCell (u8 **dest, uint *width, uint *height, int *ox, int *oy,
	const nintendo_ncer_t *ncer, uint cell_index, const nitro_ncgr_t *ncgr,
	const nitro_nclr_t *nclr);

// Renders an NSCR screen tilemap into a tightly packed RGBA8 image using
// tile data from NCGR and palette from NCLR.
enumError RenderNSCR (u8 **dest, uint *width, uint *height, const nitro_nscr_t *nscr,
	const nitro_ncgr_t *ncgr, const nitro_nclr_t *nclr);

#endif
