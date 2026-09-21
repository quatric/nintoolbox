// MPBIN member boundaries and decompression failure regressions.
#include "lib-mpbin.h"
#include "lib-archive-util.h"
#include <stdio.h>
#include <string.h>

long mpbin_fail_after = -1;
bool mpbin_allocation_failed;
static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static void expect_invalid (const u8 *data, uint size)
{
	nintendo_sarc_entry_t *entries = (void *)1;
	uint count = 99;
	CHECK (ScanMPBIN (&entries, &count, data, size) != ERR_OK);
	CHECK (!entries && !count);
}

static void expect_build_invalid (const nintendo_sarc_entry_t *entries, uint count)
{
	u8 *out = (void *)1;
	uint size = 99;
	CHECK (CreateMPBIN (&out, &size, entries, count) != ERR_OK);
	CHECK (!out && !size);
}

static void test_builder (void)
{
	nintendo_sarc_entry_t input[3] = {0};
	input[0].name = "first.bin";
	input[0].data = (const u8 *)"first member payload";
	input[0].size = strlen ((const char *)input[0].data);
	input[1].name = "second.bin";
	input[1].data = (const u8 *)"second member payload";
	input[1].size = strlen ((const char *)input[1].data);
	input[2].name = MPBIN_SETUP_FILE;
	input[2].data = (const u8 *)"compress_type=0: second.bin\ncompress_type=5: first.bin\n";
	input[2].size = strlen ((const char *)input[2].data);
	for (uint order = 0; order < 3; order++)
	{
		nintendo_sarc_entry_t ordered[3];
		uint file = 0;
		for (uint i = 0; i < 3; i++)
			ordered[i] = input[i == order ? 2 : file++];
		u8 *out = 0;
		uint size = 0;
		CHECK (CreateMPBIN (&out, &size, ordered, 3) == ERR_OK);
		if (out)
		{
			CHECK (rd_be32 (out + rd_be32 (out + 4) + 4) == MPBIN_COMP_RLE);
			CHECK (rd_be32 (out + rd_be32 (out + 8) + 4) == MPBIN_COMP_NONE);
		}
		FREE (out);
	}

	const char *invalid[] = {"file000\tcompress_type=6", "file000\tcompress_type=8",
		"file4096\tcompress_type=0", "compress_type=6: first.bin"};
	for (uint i = 0; i < sizeof invalid / sizeof *invalid; i++)
	{
		input[2].data = (const u8 *)invalid[i];
		input[2].size = strlen (invalid[i]);
		expect_build_invalid (input, 3);
	}
	input[2].data = 0;
	input[2].size = 1;
	expect_build_invalid (input, 3);
	input[2].size = UINT_MAX;
	expect_build_invalid (input, 3);
	input[0].data = 0;
	expect_build_invalid (input, 1);
	input[0].data = (const u8 *)"";
	input[0].size = 0x10000001u;
	expect_build_invalid (input, 1);

	// The creator must obey the same member limit as the reader.
	nintendo_sarc_entry_t *many = CALLOC (4097, sizeof (*many));
	CHECK (many != 0);
	if (many)
	{
		for (uint i = 0; i < 4097; i++)
			many[i].name = "empty.bin";
		u8 *out = 0;
		uint size = 0;
		CHECK (CreateMPBIN (&out, &size, many, 4000) == ERR_OK);
		CHECK (IsMPBIN (out, size));
		FREE (out);
		expect_build_invalid (many, 4001);
		expect_build_invalid (many, 4097);
		FREE (many);
	}

	// Fail each allocation in turn, including compressor buffer growth and
	// the final output allocation. No failure may return a partial archive.
	u8 payload[4096];
	u32 state = 1;
	for (uint i = 0; i < sizeof payload; i++)
	{
		state = state * 1664525u + 1013904223u;
		payload[i] = state >> 24;
	}
	input[0].data = payload;
	input[0].size = sizeof payload;
	const uint types[] = {0, 1, 2, 3, 4, 5, 7};
	for (uint type = 0; type < sizeof types / sizeof *types; type++)
	{
		char setup[128];
		snprintf (setup, sizeof setup, "file000\tcompress_type=%u\nfile001\tcompress_type=%u\n", types[type], types[type]);
		input[2].data = (const u8 *)setup;
		input[2].size = strlen (setup);
		bool completed = false;
		for (long point = 0; point < 100; point++)
		{
			mpbin_fail_after = point;
			mpbin_allocation_failed = false;
			u8 *out = (void *)1;
			uint size = 99;
			enumError err = CreateMPBIN (&out, &size, input, 3);
			mpbin_fail_after = -1;
			if (mpbin_allocation_failed)
			{
				CHECK (err != ERR_OK);
				CHECK (!out && !size);
			}
			else
			{
				CHECK (err == ERR_OK && IsMPBIN (out, size));
				nintendo_sarc_entry_t *decoded = 0;
				uint count = 0;
				CHECK (ScanMPBIN (&decoded, &count, out, size) == ERR_OK);
				CHECK (count == 3);
				if (count == 3)
				{
					CHECK (decoded[0].size == sizeof payload && !memcmp (decoded[0].data, payload, sizeof payload));
					CHECK (decoded[1].size == input[1].size && !memcmp (decoded[1].data, input[1].data, input[1].size));
				}
				ResetOwnedEntries (decoded, count);
				FREE (out);
				completed = true;
				break;
			}
		}
		CHECK (completed);
	}
}

int main (void)
{
	// Two stored members, both with a complete header and one payload byte.
	u8 valid[30] = {0};
	wr_be32 (valid, 2);
	wr_be32 (valid + 4, 12);
	wr_be32 (valid + 8, 21);
	wr_be32 (valid + 12, 1);
	valid[20] = 'A';
	wr_be32 (valid + 21, 1);
	valid[29] = 'B';
	CHECK (IsMPBIN (valid, sizeof valid));
	nintendo_sarc_entry_t *entries = 0;
	uint count = 0;
	CHECK (ScanMPBIN (&entries, &count, valid, sizeof valid) == ERR_OK);
	CHECK (count == 3);
	if (count == 3)
		CHECK (entries[0].size == 1 && entries[0].data[0] == 'A'
			&& entries[1].size == 1 && entries[1].data[0] == 'B');
	ResetOwnedEntries (entries, count);

	// Invalid later headers used to pass the first-member-only probe.
	u8 bad[30];
	memcpy (bad, valid, sizeof bad);
	wr_be32 (bad + 25, 6);
	CHECK (!IsMPBIN (bad, sizeof bad));
	expect_invalid (bad, sizeof bad);
	memcpy (bad, valid, sizeof bad);
	wr_be32 (bad + 21, 2); // Stored size exceeds its own slot.
	CHECK (!IsMPBIN (bad, sizeof bad));
	expect_invalid (bad, sizeof bad);
	memcpy (bad, valid, sizeof bad);
	wr_be32 (bad + 21, 0x10000001);
	wr_be32 (bad + 25, MPBIN_COMP_LZSS);
	CHECK (!IsMPBIN (bad, sizeof bad));
	expect_invalid (bad, sizeof bad);
	for (uint end = 21; end < 29; end++)
	{
		CHECK (!IsMPBIN (valid, end));
		expect_invalid (valid, end);
	}
	memcpy (bad, valid, sizeof bad);
	wr_be32 (bad + 8, 0xfffffffcu);
	CHECK (!IsMPBIN (bad, sizeof bad));
	expect_invalid (bad, sizeof bad);
	expect_invalid (0, 0);

	// Keep the first member valid to exercise cleanup of partially decoded lists.
	const uint types[] = {1, 2, 3, 4, 5, 7};
	for (uint i = 0; i < sizeof types / sizeof *types; i++)
	{
		memcpy (bad, valid, sizeof bad);
		wr_be32 (bad + 21, 100);
		wr_be32 (bad + 25, types[i]);
		CHECK (IsMPBIN (bad, sizeof bad));
		expect_invalid (bad, sizeof bad);
	}

	// A complete zlib stream can still be shorter than the declared member.
	const u8 plain[] = "inflate payload";
	u8 *compressed = 0;
	uint compressed_size = 0;
	CHECK (CompressMPBIN_Inflate (&compressed, &compressed_size, plain, sizeof plain) == ERR_OK);
	u8 output[sizeof plain + 1];
	if (compressed)
	{
		CHECK (DecompressMPBIN_Inflate (output, sizeof plain, compressed, compressed_size) == ERR_OK);
		CHECK (!memcmp (output, plain, sizeof plain));
		CHECK (DecompressMPBIN_Inflate (output, sizeof output, compressed, compressed_size) != ERR_OK);
		FREE (compressed);
	}
	// Valid archives must still round-trip through every supported compressor.
	const uint all_types[] = {0, 1, 2, 3, 4, 5, 7};
	for (uint i = 0; i < sizeof all_types / sizeof *all_types; i++)
	{
		char setup[100];
		snprintf (setup, sizeof setup, "file000\tcompress_type=%u\n", all_types[i]);
		nintendo_sarc_entry_t input[2] = {0};
		input[0].name = "file000.dat";
		input[0].data = plain;
		input[0].size = sizeof plain;
		input[1].name = MPBIN_SETUP_FILE;
		input[1].data = (u8 *)setup;
		input[1].size = strlen (setup);
		u8 *archive = 0;
		uint size = 0;
		CHECK (CreateMPBIN (&archive, &size, input, 2) == ERR_OK);
		entries = 0;
		count = 0;
		CHECK (ScanMPBIN (&entries, &count, archive, size) == ERR_OK);
		CHECK (count == 2);
		if (count == 2)
			CHECK (entries[0].size == sizeof plain && !memcmp (entries[0].data, plain, sizeof plain));
		ResetOwnedEntries (entries, count);
		FREE (archive);

		input[0].data = 0;
		input[0].size = 0;
		CHECK (CreateMPBIN (&archive, &size, input, 2) == ERR_OK);
		entries = 0;
		count = 0;
		CHECK (ScanMPBIN (&entries, &count, archive, size) == ERR_OK);
		CHECK (count == 2);
		if (count == 2)
			CHECK (entries[0].size == 0);
		ResetOwnedEntries (entries, count);
		FREE (archive);
	}
	u8 *empty = (void *)1;
	uint empty_size = 99;
	CHECK (CompressMPBIN_LZSS (&empty, &empty_size, 0, 0) == ERR_OK);
	CHECK (!empty && !empty_size);
	test_builder ();
	printf ("MPBIN regressions: %d failures\n", failures);
	return failures != 0;
}
