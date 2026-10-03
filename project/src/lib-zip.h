#ifndef LIB_ZIP_H
#define LIB_ZIP_H
#include "lib-std.h"
// Stored (0) or raw DEFLATE (8), with exact lengths and CRC. Null output validates.
enumError DecodeZIPMember (
	FILE *file, const u8 *data, uint stored_size, uint size, uint expected_crc, uint method);
#endif
