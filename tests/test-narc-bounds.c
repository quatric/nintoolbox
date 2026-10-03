// NARC chunk boundaries and member table integrity.
#include "lib-narc.h"
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
	narc_t archive;
	u8 tiny[24] = { 0 };
	memcpy (tiny, "NARC", 4);
	wr_le16 (tiny + 4, 0xfffe);
	memcpy (tiny + 16, "BTAF", 4);
	wr_le32 (tiny + 20, 8);
	CHECK (ScanNARC (&archive, tiny, sizeof (tiny)) != ERR_OK);
	ResetNARC (&archive);
	u8 valid[49] = { 0 };
	memcpy (valid, "NARC", 4);
	wr_le16 (valid + 4, 0xfffe);
	memcpy (valid + 16, "BTAF", 4);
	wr_le32 (valid + 20, 20);
	wr_le32 (valid + 24, 1);
	wr_le32 (valid + 32, 5);
	memcpy (valid + 36, "GMIF", 4);
	wr_le32 (valid + 40, 13);
	memcpy (valid + 44, "hello", 5);
	CHECK (ScanNARC (&archive, valid, sizeof (valid)) == ERR_OK);
	CHECK (archive.n_entries == 1 && archive.entries[0].size == 5);
	ResetNARC (&archive);
	for (uint n = 0; n < sizeof (valid); n++)
	{
		memset (&archive, 0, sizeof (archive));
		CHECK (ScanNARC (&archive, valid, n) != ERR_OK);
		CHECK (!archive.entries && !archive.n_entries);
		ResetNARC (&archive);
	}
	const uint fields[] = { 20, 24, 28, 32, 40 };
	for (uint i = 0; i < sizeof (fields) / sizeof (*fields); i++)
	{
		const uint old = rd_le32 (valid + fields[i]);
		wr_le32 (valid + fields[i], UINT_MAX);
		CHECK (ScanNARC (&archive, valid, sizeof (valid)) != ERR_OK);
		CHECK (!archive.entries && !archive.n_entries);
		ResetNARC (&archive);
		wr_le32 (valid + fields[i], old);
	}
	u8 bad_dirs[65] = { 0 };
	memcpy (bad_dirs, valid, sizeof (valid));
	memcpy (bad_dirs + 49, "BTNF", 4);
	wr_le32 (bad_dirs + 53, 16);
	wr_le16 (bad_dirs + 63, 2);
	CHECK (ScanNARC (&archive, bad_dirs, sizeof (bad_dirs)) != ERR_OK);
	CHECK (!archive.entries && !archive.n_entries);
	ResetNARC (&archive);
	wr_le32 (valid + 28, 4);
	wr_le32 (valid + 32, 3);
	CHECK (ScanNARC (&archive, valid, sizeof (valid)) != ERR_OK);
	ResetNARC (&archive);
	wr_le32 (valid + 28, 5);
	wr_le32 (valid + 32, 5);
	CHECK (ScanNARC (&archive, valid, sizeof (valid)) == ERR_OK);
	CHECK (archive.entries && archive.entries[0].size == 0);
	ResetNARC (&archive);
	printf ("NARC regressions: %d failures\n", failures);
	return failures != 0;
}
