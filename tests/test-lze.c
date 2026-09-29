// LZE input bounds, overlapping matches and registry names.
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
	const u8 data[] = { 'L', 'e', 15, 0, 0, 0, 0x4f, 'A', 'B', 'C', 'D', 'E', 'F', 1, 0, 0x10 };
	u8 *out = 0;
	uint size = 0;
	CHECK (DecodeLZE (&out, &size, data, sizeof (data)) == ERR_OK);
	CHECK (size == 15);
	if (out && size == 15)
		CHECK (!memcmp (out, "ABCDEFABCCCCCCCC", 15));
	free (out);
	for (uint n = 0; n < sizeof (data); n++)
	{
		out = (u8 *)1;
		size = 99;
		CHECK (DecodeLZE (&out, &size, data, n) == ERR_INVALID_DATA);
		CHECK (!out && !size);
	}
	const u8 invalid[][10] = { { 'L', 'e', 3, 0, 0, 0, 0, 0, 0 }, { 'L', 'e', 3, 0, 0, 0, 1, 0 },
		{ 'L', 'e', 2, 0, 0, 0, 6, 'A', 0 }, { 'L', 'e', 255, 255, 255, 255 } };
	for (uint i = 0; i < sizeof (invalid) / sizeof (*invalid); i++)
	{
		CHECK (DecodeLZE (&out, &size, invalid[i], sizeof (*invalid)) == ERR_INVALID_DATA);
		CHECK (!out && !size);
	}
	CHECK (DecodeLZE (&out, &size, 0, 0) == ERR_INVALID_DATA);
	const u8 empty[] = { 'L', 'e', 0, 0, 0, 0 };
	CHECK (DecodeLZE (&out, &size, empty, sizeof (empty)) == ERR_OK);
	CHECK (!out && !size);
	CHECK (!strcmp (GetNintendoFormatName (NFMT_LZE), "LZE"));
	CHECK (!strcmp (GetNintendoFormatName (NFMT_NTTF), "NTTF"));
	CHECK (!strcmp (GetNintendoFormatName (NFMT_SHDVAR), "SHDVAR"));
	CHECK (!strcmp (GetNintendoFormatName (NFMT_AFS), "AFS"));
	printf ("LZE regressions: %d failures\n", failures);
	return failures != 0;
}
