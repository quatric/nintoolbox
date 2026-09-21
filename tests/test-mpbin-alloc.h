// Inject allocation failures only into the MPBIN test translation units.
#ifndef TEST_MPBIN_ALLOC_H
#define TEST_MPBIN_ALLOC_H
#include "lib-std.h"

extern long mpbin_fail_after;
extern bool mpbin_allocation_failed;

static inline bool mpbin_fail_allocation (void)
{
	if (mpbin_fail_after < 0)
		return false;
	if (mpbin_fail_after-- == 0)
	{
		mpbin_allocation_failed = true;
		return true;
	}
	return false;
}

static inline void *mpbin_test_malloc (size_t size)
{
	return mpbin_fail_allocation () ? 0 : MALLOC (size);
}
static inline void *mpbin_test_calloc (size_t count, size_t size)
{
	return mpbin_fail_allocation () ? 0 : CALLOC (count, size);
}
static inline void *mpbin_test_realloc (void *ptr, size_t size)
{
	return mpbin_fail_allocation () ? 0 : REALLOC (ptr, size);
}

#undef MALLOC
#undef CALLOC
#undef REALLOC
#define MALLOC(size) mpbin_test_malloc (size)
#define CALLOC(count, size) mpbin_test_calloc (count, size)
#define REALLOC(ptr, size) mpbin_test_realloc (ptr, size)
#endif
