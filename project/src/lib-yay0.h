#ifndef LIB_YAY0_H
#define LIB_YAY0_H

#include "lib-nintendo.h"

enumError DecodeYay0 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeYay0 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

#endif
