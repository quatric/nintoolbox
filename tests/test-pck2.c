// PCK2 record bounds, names and output initialization.
#include "lib-pck2.h"
#include "lib-nintendo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef malloc
#undef calloc
#undef realloc
#undef free
#undef strdup

void trace_free (ccp f, ccp p, uint l, void *v)
{
	(void)f;
	(void)p;
	(void)l;
	free (v);
}
void *trace_malloc (ccp f, ccp p, uint l, size_t n)
{
	(void)f;
	(void)p;
	(void)l;
	return malloc (n);
}
void *trace_calloc (ccp f, ccp p, uint l, size_t n, size_t s)
{
	(void)f;
	(void)p;
	(void)l;
	return calloc (n, s);
}
void *trace_realloc (ccp f, ccp p, uint l, void *v, size_t n)
{
	(void)f;
	(void)p;
	(void)l;
	return realloc (v, n);
}
void dclib_free (void *v)
{
	free (v);
}
void *dclib_malloc (size_t n)
{
	return malloc (n);
}
void *dclib_calloc (size_t n, size_t s)
{
	return calloc (n, s);
}
void *dclib_realloc (void *v, size_t n)
{
	return realloc (v, n);
}
char *dclib_strdup (ccp s)
{
	return s ? strdup (s) : 0;
}

static int failures;
#define CHECK(expr)                                                                                \
	do                                                                                             \
	{                                                                                              \
		if (!(expr))                                                                               \
		{                                                                                          \
			fprintf (stderr, "FAIL line %d: %s\n", __LINE__, #expr);                               \
			failures++;                                                                            \
		}                                                                                          \
	} while (0)

int main (void)
{
	u8 data[64] = { 0 };
	wr_le32 (data, 16);
	wr_le32 (data + 4, sizeof (data));
	memcpy (data + 8, "PCK2", 4);
	wr_le32 (data + 16, 20);
	wr_le32 (data + 20, 24);
	wr_le32 (data + 28, 3);
	memcpy (data + 32, "one", 4);
	memcpy (data + 36, "abc", 3);
	wr_le32 (data + 40, 24);
	wr_le32 (data + 44, 24);
	memcpy (data + 56, "empty", 6);
	pck2_entry_t *entries = 0;
	uint count = 0;
	CHECK (ScanPCK2 (&entries, &count, data, sizeof (data)) == ERR_OK);
	CHECK (count == 2);
	if (count == 2)
	{
		CHECK (!strcmp (entries[0].name, "one"));
		CHECK (entries[0].size == 3 && !memcmp (entries[0].data, "abc", 3));
		CHECK (!strcmp (entries[1].name, "empty") && !entries[1].size);
	}
	free (entries);
	for (uint n = 0; n < sizeof (data); n++)
	{
		entries = (pck2_entry_t *)1;
		count = 999;
		CHECK (ScanPCK2 (&entries, &count, data, n) == ERR_INVALID_DATA);
		CHECK (!entries && !count);
	}
	const uint fields[] = { 0, 4, 16, 20, 28, 40, 44, 52 };
	for (uint i = 0; i < sizeof (fields) / sizeof (*fields); i++)
	{
		const uint old = rd_le32 (data + fields[i]);
		wr_le32 (data + fields[i], UINT_MAX);
		CHECK (ScanPCK2 (&entries, &count, data, sizeof (data)) == ERR_INVALID_DATA);
		CHECK (!entries && !count);
		wr_le32 (data + fields[i], old);
	}
	memset (data + 56, 'x', 8);
	CHECK (ScanPCK2 (&entries, &count, data, sizeof (data)) == ERR_INVALID_DATA);
	CHECK (!entries && !count);
	CHECK (ScanPCK2 (&entries, &count, 0, 0) == ERR_INVALID_DATA);
	printf ("PCK2 regressions: %d failures\n", failures);
	return failures != 0;
}
