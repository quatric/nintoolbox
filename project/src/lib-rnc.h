#ifndef LIB_RNC_H
#define LIB_RNC_H

#include "lib-nintendo.h"

enumError DecodeRNC (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeRNC (u8 **dest, uint *dest_size, const u8 *src, uint src_size, int method);

#endif
