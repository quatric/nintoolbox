#ifndef LIB_LZ10_H
#define LIB_LZ10_H

#include "lib-nintendo.h"

enumError DecodeLZ10LZ11 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeLZ10LZ11 (u8 **dest, uint *dest_size, const u8 *src, uint src_size, bool lz11);
enumError EncodeLZ10Raw (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

#endif
