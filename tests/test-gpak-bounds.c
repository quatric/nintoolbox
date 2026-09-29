// GPAK size-overflow rejection and roundtrip regressions.
#include "lib-std.h"
#include "lib-gpak.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef malloc
#undef calloc
#undef free

static bool fail_alloc;
static uint allocations;
static void *allocate (size_t n, size_t s)
{
	allocations++;
	return fail_alloc ? 0 : calloc (n, s);
}
void trace_free (ccp f, ccp p, uint l, void *v)
{
	free (v);
}
void *trace_malloc (ccp f, ccp p, uint l, size_t n)
{
	return allocate (n, 1);
}
void *trace_calloc (ccp f, ccp p, uint l, size_t n, size_t s)
{
	return allocate (n, s);
}
void dclib_free (void *v)
{
	free (v);
}
void *dclib_malloc (size_t n)
{
	return allocate (n, 1);
}
void *dclib_calloc (size_t n, size_t s)
{
	return allocate (n, s);
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
	allocations = 0;
	CHECK (CreateGPAK (&out, &size, entries, count) != ERR_OK);
	CHECK (!out && !size && !allocations);
}

int main (void)
{
	const u8 payload[] = { 1, 2, 3, 4, 5 };
	nintendo_sarc_entry_t bad = { 0, payload, UINT_MAX };
	reject (&bad, 1);
	bad.size = UINT_MAX - 31;
	reject (&bad, 1);
	nintendo_sarc_entry_t sum_overflow[] = {
		{ 0, payload, 0x80000000 },
		{ 0, payload, 0x80000000 },
	};
	reject (sum_overflow, 2);
	bad.size = 1;
	bad.data = 0;
	reject (&bad, 1);
	reject (0, 1);
	reject (&bad, 0);
	reject (&bad, UINT_MAX);
	reject (&bad, 0x1000001);

	nintendo_sarc_entry_t entries[] = {
		{ 0, 0, 0 },
		{ 0, payload, sizeof (payload) },
		{ 0, 0, 0 },
	};
	u8 *data = 0;
	uint size = 0;
	CHECK (CreateGPAK (&data, &size, entries, 3) == ERR_OK);
	if (!data)
		return 1;
	gpak_t archive;
	CHECK (ScanGPAK (&archive, data, size) == ERR_OK);
	CHECK (archive.n_entries == 3);
	if (archive.n_entries == 3)
	{
		CHECK (!archive.entries[0].size && !archive.entries[2].size);
		CHECK (archive.entries[1].size == sizeof (payload));
		CHECK (!memcmp (archive.entries[1].data, payload, sizeof (payload)));
		CHECK (archive.entries[2].data == data + size);
	}
	ResetGPAK (&archive);
	// A wrapped on-disk member range must also be rejected by the reader.
	wr_be32 (data + 16, UINT_MAX);
	wr_be32 (data + 20, 2);
	CHECK (ScanGPAK (&archive, data, size) != ERR_OK);
	free (data);

	data = (void *)1;
	size = 99;
	fail_alloc = true;
	CHECK (CreateGPAK (&data, &size, entries, 3) == ERR_OUT_OF_MEMORY);
	CHECK (!data && !size);
	fail_alloc = false;
	printf ("GPAK regressions: %d failures\n", failures);
	return failures != 0;
}
