#ifndef LIB_BLZ_H
#define LIB_BLZ_H

#include "lib-nintendo.h"

enumError DecodeBLZ (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeBLZ (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

#endif
