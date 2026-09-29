#ifndef LIB_PCK2_H
#define LIB_PCK2_H
#include "lib-std.h"
typedef struct pck2_entry_t
{
	ccp name;
	const u8 *data;
	uint size;
} pck2_entry_t;
// Allocates only the entry array. Names and payloads borrow the source buffer.
enumError ScanPCK2 (pck2_entry_t **entries, uint *count, const u8 *src, uint size);
#endif
