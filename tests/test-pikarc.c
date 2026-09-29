// Pikmin interleaved ARC/DIR records, canonical writer and malformed bounds.
#include "lib-pik1.h"
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
	u8 dir[48] = { 0 }, arc[35] = { 0 };
	wr_be32 (dir, 48);
	wr_be32 (dir + 4, 2);
	wr_be32 (dir + 8, 0);
	wr_be32 (dir + 12, 3);
	wr_be32 (dir + 16, 4);
	memcpy (dir + 20, "one", 4);
	wr_be32 (dir + 24, 32);
	wr_be32 (dir + 28, 3);
	wr_be32 (dir + 32, 12);
	memcpy (dir + 36, "folder/two", 11);
	memcpy (arc, "one", 3);
	memcpy (arc + 32, "two", 3);
	pikarc_entry_t *entries = 0;
	uint count = 0;
	CHECK (ScanPIKARC (&entries, &count, dir, sizeof dir, arc, sizeof arc) == ERR_OK);
	CHECK (count == 2);
	if (count == 2)
	{
		CHECK (!strcmp (entries[0].name, "one") && entries[0].size == 3);
		CHECK (!strcmp (entries[1].name, "folder/two") && entries[1].size == 3);
		CHECK (!memcmp (entries[1].data, "two", 3));
		u8 *dd = 0, *ad = 0;
		uint ds = 0, as = 0;
		CHECK (CreatePIKARC (&dd, &ds, &ad, &as, entries, count) == ERR_OK);
		CHECK (ds == sizeof dir && as == sizeof arc);
		CHECK (!memcmp (dd, dir, sizeof dir) && !memcmp (ad, arc, sizeof arc));
		free (dd);
		free (ad);
	}
	FreePIKARC (entries, count);
	for (uint len = 0; len < sizeof dir; len++)
	{
		u8 *copy = malloc (len ? len : 1);
		memcpy (copy, dir, len);
		CHECK (ScanPIKARC (&entries, &count, copy, len, arc, sizeof arc) != ERR_OK);
		CHECK (!entries && !count);
		free (copy);
	}
	for (uint len = 0; len < sizeof arc; len++)
	{
		CHECK (ScanPIKARC (&entries, &count, dir, sizeof dir, arc, len) != ERR_OK);
		CHECK (!entries && !count);
	}
	u8 invalid[sizeof dir];
	memcpy (invalid, dir, sizeof dir);
	wr_be32 (invalid + 32, UINT_MAX);
	CHECK (ScanPIKARC (&entries, &count, invalid, sizeof invalid, arc, sizeof arc) != ERR_OK);
	memcpy (invalid, dir, sizeof dir);
	memset (invalid + 36, 0, 12);
	CHECK (ScanPIKARC (&entries, &count, invalid, sizeof invalid, arc, sizeof arc) != ERR_OK);
	pikarc_entry_t source[] = { { "one", arc, 3 }, { "empty", 0, 0 } };
	u8 *dd = 0, *ad = 0;
	uint ds = 0, as = 0;
	CHECK (CreatePIKARC (&dd, &ds, &ad, &as, source, 2) == ERR_OK);
	CHECK (ScanPIKARC (&entries, &count, dd, ds, ad, as) == ERR_OK);
	CHECK (count == 2 && !entries[1].size);
	FreePIKARC (entries, count);
	free (dd);
	free (ad);
	source[1].size = UINT_MAX;
	source[1].data = arc;
	CHECK (CreatePIKARC (&dd, &ds, &ad, &as, source, 2) != ERR_OK);
	CHECK (!dd && !ad && !ds && !as);
	source[1].size = 1;
	source[1].data = 0;
	CHECK (CreatePIKARC (&dd, &ds, &ad, &as, source, 2) != ERR_OK);
	printf ("Pikmin archive regressions: %d failures\n", failures);
	return failures != 0;
}
