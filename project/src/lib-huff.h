#ifndef LIB_HUFF_H
#define LIB_HUFF_H

#include "lib-nintendo.h"

enumError DecodeNintendoHuff (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeNintendoHuff (
	u8 **dest, uint *dest_size, const u8 *src, uint src_size, bool four_bit);

#endif
