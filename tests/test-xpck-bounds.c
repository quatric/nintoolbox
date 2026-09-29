// XPCK extraction, roundtrip, and malformed-input regressions with in-memory I/O.
#include "lib-std.h"
#include "lib-xpck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef malloc
#undef calloc
#undef free

void trace_free (ccp f, ccp p, uint l, void *v)
{
	free (v);
}
void *trace_malloc (ccp f, ccp p, uint l, size_t n)
{
	return malloc (n);
}
void *trace_calloc (ccp f, ccp p, uint l, size_t n, size_t s)
{
	return calloc (n, s);
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

volatile int verbose = -1;
int testmode;
ccp opt_dest;
FILE *stdlog;
static const u8 *input;
static size_t input_size;
static uint writes, directories;
static enumError write_error, directory_error;
static char saved_names[8][256];
static u8 saved_data[8][16];
static uint saved_sizes[8];
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

enumError LoadFileAlloc (ccp p1, ccp p2, size_t skip, u8 **data, size_t *size, size_t max_size,
	int silent, FileAttrib_t *fatt, bool fatt_max)
{
	*data = malloc (input_size + 1);
	memcpy (*data, input, input_size);
	(*data)[input_size] = 0;
	*size = input_size;
	return ERR_OK;
}

enumError CreatePath (ccp path, bool is_pure_dir)
{
	directories++;
	return directory_error;
}

enumError SaveFile (
	ccp path, ccp p2, FileMode_t mode, const void *data, uint size, FileAttrib_t *fatt)
{
	CHECK (writes < 8 && size <= sizeof (saved_data[0]));
	if (writes < 8 && size <= sizeof (saved_data[0]))
	{
		snprintf (saved_names[writes], sizeof (saved_names[writes]), "%s", path);
		memcpy (saved_data[writes], data, size);
		saved_sizes[writes] = size;
	}
	writes++;
	return write_error;
}

static enumError extract (const u8 *data, uint size)
{
	input = data;
	input_size = size;
	writes = directories = 0;
	return ExtractXPCKArchive ("input.xpck", "output", 0);
}

static void reject_create (const nintendo_sarc_entry_t *entries, uint count)
{
	u8 *data = (void *)1;
	uint size = 99;
	CHECK (CreateXPCKArchive (&data, &size, entries, count) != ERR_OK);
	CHECK (!data && !size);
}

int main (void)
{
	stdlog = stderr;
	char long_name[104], long_path[112];
	memset (long_name, 'a', 99);
	strcpy (long_name + 99, ".bin");
	snprintf (long_path, sizeof (long_path), "output/%s", long_name);
	const u8 payload[] = { 1, 2, 3, 4 };
	nintendo_sarc_entry_t entries[] = {
		{ long_name, payload, sizeof (payload) },
		{ "middle.bin", payload, 2 },
		{ "z-empty.bin", 0, 0 },
	};
	u8 *data = 0;
	uint size = 0;
	CHECK (CreateXPCKArchive (&data, &size, entries, 3) == ERR_OK);
	if (!data)
		return 1;
	CHECK (extract (data, size) == ERR_OK);
	CHECK (writes == 3 && directories == 1);
	CHECK (!strcmp (saved_names[0], long_path));
	CHECK (!strcmp (saved_names[1], "output/middle.bin"));
	CHECK (!strcmp (saved_names[2], "output/z-empty.bin"));
	CHECK (saved_sizes[0] == 4 && !memcmp (saved_data[0], payload, 4));
	CHECK (saved_sizes[1] == 2 && !memcmp (saved_data[1], payload, 2));
	CHECK (saved_sizes[2] == 0);

	// A recognized truncated archive must fail before creating files or dirs.
	for (uint truncated = 4; truncated < size; truncated++)
	{
		CHECK (extract (data, truncated) == ERR_INVALID_DATA);
		CHECK (!writes && !directories);
	}
	u8 *bad = malloc (size);
	memcpy (bad, data, size);
	wr_le16 (bad + 0x10 + 12 + 8, 0xffff);
	bad[0x10 + 12 + 11] = 0xff;
	CHECK (extract (bad, size) == ERR_INVALID_DATA);
	CHECK (!writes && !directories);
	memcpy (bad, data, size);
	wr_le16 (bad + 14, 0xffff);
	CHECK (extract (bad, size) == ERR_INVALID_DATA);
	CHECK (!writes && !directories);
	free (bad);

	write_error = ERR_WRITE_FAILED;
	CHECK (extract (data, size) == ERR_WRITE_FAILED);
	CHECK (writes == 1);
	write_error = ERR_OK;
	directory_error = ERR_CANT_CREATE;
	CHECK (extract (data, size) == ERR_CANT_CREATE);
	CHECK (!writes);
	directory_error = ERR_OK;
	testmode = 1;
	CHECK (extract (data, size) == ERR_OK);
	CHECK (!writes && !directories);
	testmode = 0;
	free (data);

	// A minimal archive containing only an empty file ends at data_offset.
	CHECK (CreateXPCKArchive (&data, &size, entries + 2, 1) == ERR_OK);
	CHECK (extract (data, size) == ERR_OK);
	CHECK (writes == 1 && saved_sizes[0] == 0);
	free (data);

	nintendo_sarc_entry_t missing = { "missing.bin", 0, 1 };
	reject_create (&missing, 1);
	reject_create (0, 1);
	reject_create (entries, 0x1000);
	nintendo_sarc_entry_t duplicates[] = {
		{ "folder-a/member.bin", payload, 4 },
		{ "folder-b/member.bin", payload, 4 },
	};
	reject_create (duplicates, 2);
	printf ("XPCK regressions: %d failures\n", failures);
	return failures != 0;
}
