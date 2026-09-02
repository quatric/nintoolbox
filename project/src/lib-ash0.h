#ifndef LIB_ASH0_H
#define LIB_ASH0_H

#include "lib-nintendo.h"

enumError DecodeASH0 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeASH0 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

#endif
