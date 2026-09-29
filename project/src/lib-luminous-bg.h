#ifndef LIB_LUMINOUS_BG_H
#define LIB_LUMINOUS_BG_H
#include "lib-std.h"
enumError DecodeLuminousBG_RGBA (u8 **rgba, uint *width, uint *height, const u8 *map, uint map_size,
	const u8 *tiles, uint tile_size, const u8 *palette, uint palette_size);
enumError LoadLuminousBG_RGBA (
	u8 **rgba, uint *width, uint *height, const u8 *map, uint map_size, ccp filename);
#endif
