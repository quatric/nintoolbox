#ifndef LIB_NUTEXB_H
#define LIB_NUTEXB_H

#include "lib-nintendo.h"

enumError DecodeNUTEXB_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size);
enumError EncodeNUTEXB_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, ccp name);

#endif
