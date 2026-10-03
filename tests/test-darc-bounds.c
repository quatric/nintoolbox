// DARC tree validation and lossless archive creation.
#include "lib-darc.h"
#include "lib-nintendo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef malloc
#undef calloc
#undef realloc
#undef free
#undef strdup

static uint alloc_count, fail_at;
static bool fail_allocation (void)
{
	return ++alloc_count == fail_at;
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
	return fail_allocation () ? 0 : malloc (n);
}
void *trace_calloc (ccp f, ccp p, uint l, size_t n, size_t s)
{
	(void)f;
	(void)p;
	(void)l;
	return fail_allocation () ? 0 : calloc (n, s);
}
void *trace_realloc (ccp f, ccp p, uint l, void *v, size_t n)
{
	(void)f;
	(void)p;
	(void)l;
	return fail_allocation () ? 0 : realloc (v, n);
}
void dclib_free (void *v)
{
	free (v);
}
void *dclib_malloc (size_t n)
{
	return fail_allocation () ? 0 : malloc (n);
}
void *dclib_calloc (size_t n, size_t s)
{
	return fail_allocation () ? 0 : calloc (n, s);
}
void *dclib_realloc (void *v, size_t n)
{
	return fail_allocation () ? 0 : realloc (v, n);
}
char *dclib_strdup (ccp s)
{
	if (!s || fail_allocation ())
		return 0;
	return strdup (s);
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
	const u8 payload[] = "hello";
	nintendo_sarc_entry_t input = { .name = "first.bin", .data = payload, .size = 5 };
	u8 *data = 0;
	uint size = 0;
	darc_t archive = { 0 };
	CHECK (CreateDARC (&data, &size, &input, 1) == ERR_OK);
	CHECK (data && ScanDARC (&archive, data, size) == ERR_OK);
	CHECK (archive.n_entries == 2 && !strcmp (archive.entries[1].name, "first.bin"));
	ResetDARC (&archive);
	for (uint n = 0; n < size; n++)
	{
		CHECK (ScanDARC (&archive, data, n) != ERR_OK);
		CHECK (!archive.entries && !archive.n_entries);
		ResetDARC (&archive);
	}
	const uint fields[] = { 12, 16, 20, 24, 32, 36, 40, 44, 48 };
	const uint values[] = { 28, UINT_MAX, UINT_MAX, 0, 1, 3, 0xffffff, 0, UINT_MAX };
	for (uint i = 0; i < sizeof (fields) / sizeof (*fields); i++)
	{
		const uint old = rd_le32 (data + fields[i]);
		wr_le32 (data + fields[i], values[i]);
		CHECK (ScanDARC (&archive, data, size) != ERR_OK);
		ResetDARC (&archive);
		wr_le32 (data + fields[i], old);
	}
	free (data);
	input.name = "café/雪😀.bin";
	CHECK (CreateDARC (&data, &size, &input, 1) == ERR_OK);
	CHECK (ScanDARC (&archive, data, size) == ERR_OK);
	CHECK (archive.n_entries == 3 && !strcmp (archive.entries[1].name, "café")
		&& !strcmp (archive.entries[2].name, "雪😀.bin"));
	ResetDARC (&archive);
	free (data);

	char deep[256];
	memset (deep, 0, sizeof (deep));
	for (uint i = 0; i < 100; i++)
		strcat (deep, "d/");
	strcat (deep, "file.bin");
	input.name = deep;
	CHECK (CreateDARC (&data, &size, &input, 1) == ERR_OK);
	CHECK (ScanDARC (&archive, data, size) == ERR_OK);
	CHECK (archive.n_entries == 102);
	ResetDARC (&archive);
	free (data);

	const char *invalid[] = { "../file", "a/../file", "a//file", "/file", "file/", "file\\name",
		"a:bad", "bad\xc0\xaf", "bad\xed\xa0\x80", "bad\xf4\x90\x80\x80", "bad\xe2\x82" };
	for (uint i = 0; i < sizeof (invalid) / sizeof (*invalid); i++)
	{
		input.name = invalid[i];
		data = (u8 *)1;
		size = 123;
		CHECK (CreateDARC (&data, &size, &input, 1) != ERR_OK);
		CHECK (!data && !size);
	}
	input.name = "file";
	input.size = UINT_MAX;
	CHECK (CreateDARC (&data, &size, &input, 1) == ERR_FILE_TOO_BIG);
	CHECK (!data && !size);
	input.size = 5;
	nintendo_sarc_entry_t collisions[] = { input, input };
	CHECK (CreateDARC (&data, &size, collisions, 2) != ERR_OK);
	collisions[1].name = "file/child";
	CHECK (CreateDARC (&data, &size, collisions, 2) != ERR_OK);
	collisions[0].name = "file/child";
	collisions[1].name = "file";
	CHECK (CreateDARC (&data, &size, collisions, 2) != ERR_OK);

	input.name = "nested/雪😀.bin";
	alloc_count = 0;
	CHECK (CreateDARC (&data, &size, &input, 1) == ERR_OK);
	const uint create_allocs = alloc_count;
	alloc_count = 0;
	CHECK (ScanDARC (&archive, data, size) == ERR_OK);
	const uint scan_allocs = alloc_count;
	ResetDARC (&archive);
	for (uint i = 1; i <= scan_allocs; i++)
	{
		alloc_count = 0;
		fail_at = i;
		CHECK (ScanDARC (&archive, data, size) == ERR_OUT_OF_MEMORY);
		CHECK (!archive.entries && !archive.n_entries);
		ResetDARC (&archive);
	}
	fail_at = 0;
	free (data);
	for (uint i = 1; i <= create_allocs; i++)
	{
		alloc_count = 0;
		fail_at = i;
		CHECK (CreateDARC (&data, &size, &input, 1) == ERR_OUT_OF_MEMORY);
		CHECK (!data && !size);
	}
	fail_at = 0;

	enum
	{
		COUNT = 600
	};
	nintendo_sarc_entry_t *many = calloc (COUNT, sizeof (*many));
	char (*names)[32] = calloc (COUNT, sizeof (*names));
	for (uint i = 0; i < COUNT; i++)
	{
		snprintf (names[i], sizeof (names[i]), "dir_%04u/file.bin", i);
		many[i].name = names[i];
		many[i].data = payload;
		many[i].size = 5;
	}
	CHECK (CreateDARC (&data, &size, many, COUNT) == ERR_OK);
	CHECK (ScanDARC (&archive, data, size) == ERR_OK);
	CHECK (archive.n_entries == 1 + 2 * COUNT);
	ResetDARC (&archive);
	free (data);
	free (names);
	free (many);
	printf ("DARC regressions: %d failures\n", failures);
	return failures != 0;
}
