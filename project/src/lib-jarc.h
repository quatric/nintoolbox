#ifndef LIB_JARC_H
#define LIB_JARC_H

#include "lib-nintendo.h"

typedef struct jarc_entry_t
{
	char name[256];
	const u8 *data;
	u32 size;
	u32 offset;
	char ext[16];
} jarc_entry_t;

typedef struct jarc_t
{
	const u8 *raw;
	size_t raw_size;
	u8 *decomp_buffer;
	uint decomp_size;
	bool is_big_endian;
	jarc_entry_t *entries;
	uint n_entries;
} jarc_t;

void ResetJARC (jarc_t *jarc);
enumError DecodeJCMP (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError ScanJARC (jarc_t *jarc, const u8 *data, size_t size);

#endif
