// ZTAB builder bounds, allocation failures, and numeric entry ordering.
#include "lib-ztab.h"
#include "lib-archive-util.h"
#include <zlib.h>
#include "lib-nintendo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef malloc
#undef calloc
#undef realloc
#undef free
#undef strdup

static long fail_after = -1;
static int allocation_failed;
static int reject_allocation (void)
{
	if (fail_after < 0)
		return 0;
	if (fail_after-- == 0)
	{
		allocation_failed = 1;
		return 1;
	}
	return 0;
}

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
	return reject_allocation () ? 0 : malloc (n);
}
void *trace_calloc (ccp f, ccp p, uint l, size_t n, size_t s)
{
	(void)f;
	(void)p;
	(void)l;
	return reject_allocation () ? 0 : calloc (n, s);
}
void *trace_realloc (ccp f, ccp p, uint l, void *v, size_t n)
{
	(void)f;
	(void)p;
	(void)l;
	return reject_allocation () ? 0 : realloc (v, n);
}
void dclib_free (void *v)
{
	free (v);
}
void *dclib_malloc (size_t n)
{
	return reject_allocation () ? 0 : malloc (n);
}
void *dclib_calloc (size_t n, size_t s)
{
	return reject_allocation () ? 0 : calloc (n, s);
}
void *dclib_realloc (void *v, size_t n)
{
	return reject_allocation () ? 0 : realloc (v, n);
}
char *dclib_strdup (ccp s)
{
	return s ? strdup (s) : 0;
}

static int failures;
#define CHECK(cond)                                                                                \
	do                                                                                             \
	{                                                                                              \
		if (!(cond))                                                                               \
		{                                                                                          \
			fprintf (stderr, "FAIL line %d: %s\n", __LINE__, #cond);                               \
			failures++;                                                                            \
		}                                                                                          \
	} while (0)

static void reject (const nintendo_sarc_entry_t *entries, uint count)
{
	u8 *out = (void *)1;
	uint size = 99;
	CHECK (CreateZTABArchive (&out, &size, entries, count) != ERR_OK);
	CHECK (!out && !size);
}

int main (void)
{
	const u8 byte = 0x42;
	nintendo_sarc_entry_t invalid = { "payload.bin", &byte, UINT_MAX };
	reject (&invalid, 1);
	invalid.data = 0;
	invalid.size = 1;
	reject (&invalid, 1);
	reject (&invalid, 100001);
	reject (0, 1);

	nintendo_sarc_entry_t valid[] = { { "entry_0001_flags_00000002.bin", 0, 0 },
		{ "entry_0000_flags_00000001.bin", &byte, 1 } };
	int completed = 0;
	for (long point = 0; point < 10; point++)
	{
		fail_after = point;
		allocation_failed = 0;
		u8 *out = (void *)1;
		uint size = 99;
		enumError err = CreateZTABArchive (&out, &size, valid, 2);
		fail_after = -1;
		if (allocation_failed)
			CHECK (err != ERR_OK && !out && !size);
		else
		{
			CHECK (err == ERR_OK && out);
			if (out)
			{
				CHECK (!memcmp (out, "ZTAB", 4) && rd_be32 (out + 4) == 2);
				CHECK (rd_be32 (out + 8) == 1 && rd_be32 (out + 24) == 2);
				CHECK (out[rd_be32 (out + 12)] == byte);
				CHECK (rd_be32 (out + 28) == size && rd_be32 (out + 32) == 0);
				free (out);
			}
			completed = 1;
			break;
		}
	}
	CHECK (completed);

	const uint count = 10001;
	nintendo_sarc_entry_t *many = calloc (count, sizeof (*many));
	char (*names)[64] = calloc (count, sizeof (*names));
	CHECK (many && names);
	if (many && names)
	{
		for (uint i = 0; i < count; i++)
		{
			snprintf (names[i], sizeof names[i], "entry_%04u_flags_%08x.bin", i, i);
			many[i].name = names[i];
			many[i].data = &byte;
			many[i].size = 1;
		}
		u8 *out = 0;
		uint size = 0;
		CHECK (CreateZTABArchive (&out, &size, many, count) == ERR_OK);
		if (out)
			for (uint i = 0; i < count; i++)
				CHECK (rd_be32 (out + 8 + i * 16) == i);
		free (out);
	}
	free (names);
	free (many);
	printf ("ZTAB builder regressions: %d failures\n", failures);
	return failures != 0;
}
