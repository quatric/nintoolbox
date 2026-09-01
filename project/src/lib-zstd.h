#ifndef SZS_LIB_ZSTD_H
#define SZS_LIB_ZSTD_H 1

#define _GNU_SOURCE 1

#include "lib-std.h"

#define ZSTD_MAGIC_LE 0xFD2FB528u
#define ZSTD_DEFAULT_COMPR 3

// returns
// -1:    not ZSTD data
//	1..22: seems to be ZSTD data
int IsZSTD (
	cvp data, // NULL or data to investigate
	uint size // size of 'data'
);

int CalcCompressionLevelZSTD (
	int compr_level // valid 1..22 / 0: use default value
);

ccp GetMessageZSTD (
	size_t code, // error code
	ccp unknown_error // fallback
);

enumError EncodeZSTDbuf (
	void *dest,
	uint dest_size,
	uint *dest_written,
	const void *src,
	uint src_size,
	int compr_level
);

enumError EncodeZSTD (
	u8 **dest_ptr,
	uint *dest_written,
	const void *src,
	uint src_size,
	int compr_level
);

enumError DecodeZSTD (
	u8 **dest_ptr,
	uint *dest_written,
	const void *src,
	uint src_size
);

enumError DecodeZSTDpart (
	void *dest_buf,
	uint dest_size,
	uint *dest_written,
	const void *src,
	uint src_size
);

#endif // SZS_LIB_ZSTD_H 1
