// MDR builder bounds, allocation failures, and chunk order.
#include "lib-mdr.h"
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
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static void expect_invalid (const nintendo_sarc_entry_t *entries, uint count)
{
	u8 *out = (void *)1;
	uint size = 99;
	CHECK (CreateMDRArchive (&out, &size, entries, count) != ERR_OK);
	CHECK (!out && !size);
}

int main (void)
{
	const u8 payload[] = "compressed MDR payload";
	nintendo_sarc_entry_t entries[] = {
		{ "chunk_02_flags_00000003_raw.bin", payload, 3 },
		{ "chunk_00_flags_00000001_zlib.bin", payload, sizeof payload },
		{ "chunk_01_flags_00000002_raw.bin", 0, 0 }
	};
	nintendo_sarc_entry_t bad = { "chunk_raw.bin", payload, UINT_MAX };
	expect_invalid (&bad, 1);
	bad.name = "compressed.bin";
	expect_invalid (&bad, 1);
	bad.size = 1;
	bad.data = 0;
	expect_invalid (&bad, 1);
	expect_invalid (&bad, 100001);
	expect_invalid (0, 1);
	expect_invalid (entries, 0);
	// The total raw size must be checked before reading any large payload.
	nintendo_sarc_entry_t too_large[] = {
		{ "a_raw.bin", payload, 0x80000000u },
		{ "b_raw.bin", payload, 0x80000000u }
	};
	expect_invalid (too_large, 2);

	int completed = 0;
	for (long point = 0; point < 30; point++)
	{
		u8 *out = (void *)1;
		uint size = 99;
		fail_after = point;
		allocation_failed = 0;
		enumError err = CreateMDRArchive (&out, &size, entries, 3);
		fail_after = -1;
		if (allocation_failed)
		{
			CHECK (err != ERR_OK);
			CHECK (!out && !size);
		}
		else
		{
			CHECK (err == ERR_OK && IsMDR (out, size));
			if (out)
			{
				const uint first = rd_be32 (out + 4), second = rd_be32 (out + 8), third = rd_be32 (out + 12);
				CHECK (rd_be32 (out + first + 4) == 1);
				CHECK (rd_be32 (out + second + 4) == 2 && rd_be32 (out + second) == 0);
				CHECK (rd_be32 (out + third + 4) == 3 && rd_be32 (out + third + 12) == 3);
				CHECK (!memcmp (out + third + 16, payload, 3));
				u8 plain[sizeof payload];
				uLongf plain_size = sizeof plain;
				CHECK (uncompress (plain, &plain_size, out + first + 16, rd_be32 (out + first + 12)) == Z_OK);
				CHECK (plain_size == sizeof payload && !memcmp (plain, payload, sizeof payload));
				CHECK (second == ((first + 16 + rd_be32 (out + first + 12) + 1) & ~1u));
				free (out);
			}
			completed = 1;
			break;
		}
	}
	CHECK (completed);

	// Two-digit chunk names sort incorrectly once the index reaches 100.
	nintendo_sarc_entry_t many[105];
	char names[105][64];
	u8 values[105];
	for (uint i = 0; i < 105; i++)
	{
		snprintf (names[i], sizeof names[i], "chunk_%02u_flags_00000000_raw.bin", i);
		values[i] = i;
		many[i].name = names[i];
		many[i].data = values + i;
		many[i].size = 1;
	}
	u8 *out = 0;
	uint size = 0;
	CHECK (CreateMDRArchive (&out, &size, many, 105) == ERR_OK);
	CHECK (IsMDR (out, size));
	if (out)
		for (uint i = 0; i < 105; i++)
			CHECK (out[rd_be32 (out + 4 + i * 4) + 16] == i);
	free (out);
	printf ("MDR builder regressions: %d failures\n", failures);
	return failures != 0;
}
