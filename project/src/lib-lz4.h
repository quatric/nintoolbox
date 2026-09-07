#ifndef SZS_LIB_LZ4_H
#define SZS_LIB_LZ4_H 1

#define _GNU_SOURCE 1

#include "lib-std.h"

#define LZ4_MAGIC_LE 0x184D2204u
#define LZ4_DEFAULT_COMPR 0

// returns
// -1:    not LZ4 data
//	1..12: seems to be LZ4 data
int IsLZ4 (cvp data, // NULL or data to investigate
	uint size // size of 'data'
);

int CalcCompressionLevelLZ4 (int compr_level // valid 1..12 / 0: use default (fast) value
);

ccp GetMessageLZ4 (size_t code, // error code
	ccp unknown_error // fallback
);

enumError EncodeLZ4buf (void *dest, uint dest_size, uint *dest_written, const void *src,
	uint src_size, int compr_level);

enumError EncodeLZ4 (
	u8 **dest_ptr, uint *dest_written, const void *src, uint src_size, int compr_level);

enumError DecodeLZ4 (u8 **dest_ptr, uint *dest_written, const void *src, uint src_size);

enumError DecodeLZ4part (
	void *dest_buf, uint dest_size, uint *dest_written, const void *src, uint src_size);

#endif // SZS_LIB_LZ4_H 1
