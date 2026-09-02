#ifndef LIB_SZE_H
#define LIB_SZE_H

#include "lib-nintendo.h"

enumError DecodeSZE (
	u8 **dest, uint *dest_size, const u8 *data, uint size, const u8 key[16]);
enumError EncodeSZE (
	u8 **dest, uint *dest_size, const u8 *data, uint size, const u8 key[16], const u8 iv[16], uint mode);

#endif
