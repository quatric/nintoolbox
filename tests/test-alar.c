// Bounded ALAR v2/v3 records and type-3 name/record association.
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

static void fixture (u8 *d, bool v3)
{
	memset (d, 0, 128);
	memcpy (d, "ALAR", 4);
	d[4] = v3 ? 3 : 2;
	wr_le16 (d + 6, 2);
	uint first = 16, second = 32;
	if (v3)
	{
		wr_le16 (d + 16, 112);
		wr_le16 (d + 18, 24);
		wr_le16 (d + 20, 60);
		first = 24;
		second = 60;
		memcpy (d + 42, "alpha.bin", 10);
		memcpy (d + 78, "beta.bin", 9);
	}
	else
	{
		memcpy (d + 50, "alpha.bin", 10);
		memcpy (d + 90, "beta.bin", 9);
	}
	wr_le32 (d + first + 4, v3 ? 112 : 84);
	wr_le32 (d + first + 8, 4);
	wr_le32 (d + second + 4, v3 ? 116 : 124);
	wr_le32 (d + second + 8, 4);
	memcpy (d + (v3 ? 112 : 84), "abcd", 4);
	memcpy (d + (v3 ? 116 : 124), "wxyz", 4);
}
int main (void)
{
	for (int type = 0; type < 2; type++)
	{
		u8 d[128];
		fixture (d, type);
		uint size = type ? 120 : 128;
		alar_entry_t entry;
		CHECK (ReadALAREntry (&entry, d, size, 0) == ERR_OK);
		CHECK (entry.name && !strcmp (entry.name, "alpha.bin"));
		CHECK (entry.size == 4 && !memcmp (entry.data, "abcd", 4));
		CHECK (ReadALAREntry (&entry, d, size, 1) == ERR_OK);
		CHECK (entry.name && !strcmp (entry.name, "beta.bin"));
		CHECK (entry.size == 4 && !memcmp (entry.data, "wxyz", 4));
		CHECK (ReadALAREntry (&entry, d, size, 2) != ERR_OK);
		u8 *out = 0;
		uint out_size = 0;
		CHECK (DecodeALAR (&out, &out_size, d, size) == ERR_OK);
		CHECK (out_size == 4 && !memcmp (out, "abcd", 4));
		free (out);
		for (uint n = 0; n < size; n++)
		{
			u8 *short_data = malloc (n ? n : 1);
			memcpy (short_data, d, n);
			CHECK (ReadALAREntry (&entry, short_data, n, 1) != ERR_OK);
			free (short_data);
		}
		uint rec = type ? 60 : 32;
		wr_le32 (d + rec + 8, UINT32_MAX);
		CHECK (ReadALAREntry (&entry, d, size, 1) != ERR_OK);
		fixture (d, type);
		wr_le32 (d + rec + 4, UINT32_MAX);
		CHECK (ReadALAREntry (&entry, d, size, 1) != ERR_OK);
		fixture (d, type);
		wr_le16 (d + 6, UINT16_MAX);
		CHECK (ReadALAREntry (&entry, d, size, 0) != ERR_OK);
		if (type)
		{
			fixture (d, type);
			wr_le16 (d + 18, 0xfffe);
			CHECK (ReadALAREntry (&entry, d, size, 0) != ERR_OK);
			fixture (d, type);
			memset (d + 78, 'X', 34);
			CHECK (ReadALAREntry (&entry, d, size, 1) != ERR_OK);
		}
	}
	printf ("ALAR regressions: %d failures\n", failures);
	return failures != 0;
}
