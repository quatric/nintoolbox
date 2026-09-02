#ifndef LIB_GFA_H
#define LIB_GFA_H

#include "lib-nintendo.h"

typedef struct gfa_entry_t
{
	ccp name;
	u32 offset;
	u32 size;
} gfa_entry_t;

typedef struct gfa_t
{
	u8 *blob;
	uint blob_size;
	gfa_entry_t *entries;
	uint n_entries;
	char *names;
	uint compression;
} gfa_t;

void ResetGFA (gfa_t *gfa);
enumError ScanGFA (gfa_t *gfa, const u8 *data, uint size);
enumError CreateGFA (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries);

enumError DecodeLZ10Raw (u8 *dest, uint dest_size, const u8 *src, uint src_size);
enumError EncodeLZ10Raw (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError DecodeBPE (u8 *dest, uint dest_size, const u8 *src, uint src_size);

#endif
