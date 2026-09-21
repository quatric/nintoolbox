// MPBIN member boundaries and decompression failure regressions.
#include "lib-mpbin.h"
#include "lib-archive-util.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static void expect_invalid (const u8 *data, uint size)
{
	nintendo_sarc_entry_t *entries = (void *)1;
	uint count = 99;
	CHECK (ScanMPBIN (&entries, &count, data, size) != ERR_OK);
	CHECK (!entries && !count);
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
	}
	printf ("MPBIN regressions: %d failures\n", failures);
	return failures != 0;
}
