// IEAR directory bounds and typed payload extraction.
#include "lib-iear.h"
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
	u8 data[99] = { 0 };
	memcpy (data, "MAIN", 4);
	wr_le32 (data + 4, 2);
	memcpy (data + 16, "JTBL", 4);
	wr_le32 (data + 20, 4);
	wr_le32 (data + 32, 48);
	wr_le32 (data + 36, 19);
	wr_le32 (data + 40, 67);
	wr_le32 (data + 44, 16);
	memcpy (data + 48, "NCLR", 4);
	wr_le32 (data + 52, 3);
	memcpy (data + 64, "abc", 3);
	memcpy (data + 67, "../x", 4);
	memcpy (data + 83, "ENDT", 4);
	iear_entry_t *entries = 0;
	uint count = 0;
	CHECK (ScanIEAR (&entries, &count, data, sizeof (data)) == ERR_OK);
	CHECK (count == 2);
	if (count == 2)
	{
		CHECK (entries[0].size == 3 && !memcmp (entries[0].data, "abc", 3));
		CHECK (!strcmp (entries[0].extension, "nclr"));
		CHECK (entries[1].size == 0 && !strcmp (entries[1].extension, "bin"));
	}
	free (entries);
	for (uint n = 0; n < sizeof (data); n++)
	{
		entries = (iear_entry_t *)1;
		count = 999;
		CHECK (ScanIEAR (&entries, &count, data, n) == ERR_INVALID_DATA);
		CHECK (!entries && !count);
	}
	const uint fields[] = { 4, 20, 32, 36, 40, 44, 52, 71 };
	for (uint i = 0; i < sizeof (fields) / sizeof (*fields); i++)
	{
		const uint old = rd_le32 (data + fields[i]);
		wr_le32 (data + fields[i], UINT_MAX);
		CHECK (ScanIEAR (&entries, &count, data, sizeof (data)) == ERR_INVALID_DATA);
		CHECK (!entries && !count);
		wr_le32 (data + fields[i], old);
	}
	CHECK (ScanIEAR (&entries, &count, 0, 0) == ERR_INVALID_DATA);
	printf ("IEAR regressions: %d failures\n", failures);
	return failures != 0;
}
