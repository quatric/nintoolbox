#ifndef LIB_LZH8_H
#define LIB_LZH8_H

#include "lib-nintendo.h"

enumError DecodeLZH8 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeLZH8 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

#endif
