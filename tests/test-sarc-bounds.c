// SARC metadata and member bounds in both byte orders.
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
	for (uint be = 0; be < 2; be++)
	{
		void (*w16) (u8 *, u16) = be ? wr_be16 : wr_le16;
		void (*w32) (u8 *, u32) = be ? wr_be32 : wr_le32;
		u8 valid[77] = { 0 };
		memcpy (valid, "SARC", 4);
		w16 (valid + 4, 20);
		w16 (valid + 6, 0xfeff);
		w32 (valid + 8, sizeof (valid));
		w32 (valid + 12, 72);
		memcpy (valid + 20, "SFAT", 4);
		w16 (valid + 24, 12);
		w16 (valid + 26, 2);
		w32 (valid + 44, 2);
		w32 (valid + 56, 2);
		w32 (valid + 60, 5);
		memcpy (valid + 64, "SFNT", 4);
		w16 (valid + 68, 8);
		memcpy (valid + 72, "hello", 5);
		nintendo_sarc_t archive;
		CHECK (ScanSARC (&archive, valid, sizeof (valid)) == ERR_OK);
		ccp name = (ccp)1;
		const u8 *data = 0;
		uint size = 0;
		CHECK (GetSARCEntry (&archive, 1, &name, &data, &size) == ERR_OK);
		CHECK (!name && size == 3 && !memcmp (data, "llo", 3));
		for (uint n = 0; n < sizeof (valid); n++)
		{
			CHECK (ScanSARC (&archive, valid, n) != ERR_OK);
			CHECK (!archive.data && !archive.n_entries);
		}
		const uint fields[] = { 8, 12, 52, 56, 60 };
		const uint values[] = { 76, 64, 0x01000000, 6, UINT_MAX };
		for (uint i = 0; i < sizeof (fields) / sizeof (*fields); i++)
		{
			u8 bad[sizeof (valid)];
			memcpy (bad, valid, sizeof (bad));
			w32 (bad + fields[i], values[i]);
			CHECK (ScanSARC (&archive, bad, sizeof (bad)) != ERR_OK);
		}
		u8 extended[81];
		memcpy (extended, valid, sizeof (valid));
		memcpy (extended + sizeof (valid), "tail", 4);
		CHECK (ScanSARC (&archive, extended, sizeof (extended)) == ERR_OK);
		CHECK (archive.size == sizeof (valid));
		w32 (extended + 60, 9);
		CHECK (ScanSARC (&archive, extended, sizeof (extended)) != ERR_OK);
		CHECK (!archive.data && !archive.n_entries);

		// Extended SFNT headers place names after the declared header length.
		u8 named[83] = { 0 };
		memcpy (named, valid, 72);
		w32 (named + 8, sizeof (named));
		w32 (named + 12, 80);
		w32 (named + 44, 0);
		w32 (named + 52, 0x01000000);
		w32 (named + 56, 0);
		w32 (named + 60, 3);
		w16 (named + 68, 12);
		memcpy (named + 72, "xxxxfoo", 7);
		memcpy (named + 80, "abc", 3);
		CHECK (ScanSARC (&archive, named, sizeof (named)) == ERR_OK);
		CHECK (GetSARCEntry (&archive, 1, &name, &data, &size) == ERR_OK);
		CHECK (name && !strcmp (name, "foo") && size == 3 && !memcmp (data, "abc", 3));
		named[79] = 'x';
		CHECK (ScanSARC (&archive, named, sizeof (named)) != ERR_OK);
	}
	printf ("SARC regressions: %d failures\n", failures);
	return failures != 0;
}
