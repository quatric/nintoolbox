#ifndef LIB_FLIM_H
#define LIB_FLIM_H

#include "lib-nintendo.h"

static inline u8 expand5 (u8 value)
{
	return value << 3 | value >> 2;
}

static inline uint morton8 (uint x, uint y)
{
	return (x & 1) | (y & 1) << 1 | (x & 2) << 1 | (y & 2) << 2 | (x & 4) << 2 | (y & 4) << 3;
}

enumError decode_etc1_tiled (u8 *rgba, const u8 *src, uint w, uint h, uint data_size);
enumError decode_etc1a4_tiled (u8 *rgba, const u8 *src, uint w, uint h, uint data_size);

enumError DecodeFLIM_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size);
enumError EncodeFLIM_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, bool bclim);

#endif
