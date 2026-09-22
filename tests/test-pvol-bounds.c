// PVOL builder input, size, and allocation regressions.
#include "lib-pvol.h"
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

static void reject (const nintendo_sarc_entry_t *entries, uint count)
{
	u8 *out = (void *)1;
	uint size = 99;
	CHECK (CreatePVOLArchive (&out, &size, entries, count) != ERR_OK);
	CHECK (!out && !size);
}

int main (void)
{
	const u8 payload[] = "payload";
	nintendo_sarc_entry_t bad = { "payload.bin", payload, UINT_MAX };
	reject (&bad, 1);
	bad.size = 1;
	bad.data = 0;
	reject (&bad, 1);
	reject (&bad, 100000);
	reject (0, 1);
	nintendo_sarc_entry_t duplicates[] = {
		{ "folder1/member.bin", payload, sizeof payload },
		{ "folder2/member.bin", payload, sizeof payload }
	};
	reject (duplicates, 2);
	bad.name = "12345678901234567890123456789012345678901";
	bad.data = payload;
	reject (&bad, 1);

	nintendo_sarc_entry_t valid[] = {
		{ "z-empty.bin", 0, 0 },
		{ "1234567890123456789012345678901234567890", payload, sizeof payload }
	};
	int completed = 0;
	for (long point = 0; point < 10; point++)
	{
		fail_after = point;
		allocation_failed = 0;
		u8 *out = (void *)1;
		uint size = 99;
		enumError err = CreatePVOLArchive (&out, &size, valid, 2);
		fail_after = -1;
		if (allocation_failed)
		{
			CHECK (err != ERR_OK && !out && !size);
		}
		else
		{
			CHECK (err == ERR_OK && out);
			if (out)
			{
				CHECK (rd_le32 (out) == 3);
				uint first = rd_le32 (out + 4), second = rd_le32 (out + 12);
				CHECK (first == 32 && second + 40 <= size);
				CHECK (!memcmp (out + first, valid[1].name, 40));
				CHECK (!memcmp (out + first + 40, payload, sizeof payload));
				CHECK (rd_le32 (out + 16) == 0);
				free (out);
			}
			completed = 1;
			break;
		}
	}
	CHECK (completed);
	nintendo_sarc_entry_t ordered[] = {
		{ "z-folder/a.bin", payload, sizeof payload },
		{ "a-folder/z.bin", payload, sizeof payload }
	};
	u8 *out = 0;
	uint size = 0;
	CHECK (CreatePVOLArchive (&out, &size, ordered, 2) == ERR_OK);
	if (out)
	{
		CHECK (!strcmp ((const char *)out + rd_le32 (out + 4), "z.bin"));
		CHECK (!strcmp ((const char *)out + rd_le32 (out + 12), "a.bin"));
		free (out);
	}
	printf ("PVOL builder regressions: %d failures\n", failures);
	return failures != 0;
}
