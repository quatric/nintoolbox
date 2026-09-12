#ifndef LIB_CMAB_H
#define LIB_CMAB_H

#include "lib-std.h"

// Grezzo's 3DS CMAB material-animation files can embed a txpt texture table.
// The offsets are little endian and relative to bases held in the header.
typedef struct cmab_t
{
	const u8 *data;
	uint size;
	uint texture_count;
	uint texture_table;
	uint name_table;
	uint name_count;
	uint data_base;
} cmab_t;

typedef struct cmab_entry_t
{
	const u8 *data;
	uint data_size;
	uint width;
	uint height;
	uint format;
	uint pica_format;
	uint id;
	char name[PATH_MAX];
} cmab_entry_t;

enumError ScanCMAB (cmab_t *cmab, const u8 *data, uint size);
enumError GetCMABEntry (const cmab_t *cmab, uint index, cmab_entry_t *entry);
enumError DecodeCMABTexture_RGBA (
	u8 **dest, uint *width, uint *height, const cmab_entry_t *entry);

#endif
