#ifndef LIB_NINTENDO_RL_H
#define LIB_NINTENDO_RL_H

#include "lib-nintendo.h"

enumError DecodeNintendoRL (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeNintendoRL (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

#endif
