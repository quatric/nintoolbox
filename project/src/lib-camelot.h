#ifndef LIB_CAMELOT_H
#define LIB_CAMELOT_H

#include "lib-nintendo.h"

enumError DecodeCamelot (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeCamelot (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

#endif
