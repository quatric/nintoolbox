#ifndef LIB_IEAR_H
#define LIB_IEAR_H
#include "lib-std.h"
typedef struct iear_entry_t
{
	const u8 *data;
	uint size;
	char extension[5];
} iear_entry_t;
// Entry arrays are owned; payload pointers borrow the input buffer.
enumError ScanIEAR (iear_entry_t **entries, uint *count, const u8 *src, uint size);
#endif
