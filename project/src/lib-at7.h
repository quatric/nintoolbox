#ifndef LIB_AT7_H
#define LIB_AT7_H

#include "lib-nintendo.h"

enumError DecodeAT7 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeAT7 (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError CreateAT7 (u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries,
	uint n_entries, bool compress);

#endif
