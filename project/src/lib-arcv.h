#ifndef SZS_LIB_ARCV_H
#define SZS_LIB_ARCV_H 1

#include "lib-std.h"

// Namco / Tose Wii archive format (ARCV). See lib-arcv.c.
//
// Shared with lib-gpak.c: GPAK's own ordinal-named "file_%04u*" members use
// the same collect-then-sort-by-index shape.
typedef struct arcv_member_t
{
	uint index;
	u8 *data;
	size_t size;
} arcv_member_t;

bool looks_like_arcv_dir (ccp source);
int cmp_arcv_member (const void *a, const void *b);
enumError create_arcv_dir (ccp source, ccp dest);

#endif
