#include "lib-zip.h"
#include <zlib.h>
#include <string.h>
static int failures;
#define CHECK(x)                                                                                   \
	do                                                                                             \
	{                                                                                              \
		if (!(x))                                                                                  \
		{                                                                                          \
			fprintf (stderr, "FAIL line %d: %s\n", __LINE__, #x);                                  \
			failures++;                                                                            \
		}                                                                                          \
	} while (0)
int main (void)
{
	const u8 payload[] = "ZIP payload and overlap overlap overlap";
	const uint size = sizeof (payload) - 1;
	const uint crc = crc32 (0, payload, size);
	u8 compressed[256];
	uLongf compressed_size = sizeof (compressed);
	CHECK (compress2 (compressed, &compressed_size, payload, size, 9) == Z_OK);
	const u8 *raw = compressed + 2;
	const uint raw_size = compressed_size - 6;
	CHECK (DecodeZIPMember (0, raw, raw_size, size, crc, 8) == ERR_OK);
	CHECK (DecodeZIPMember (0, payload, size, size, crc, 0) == ERR_OK);
	CHECK (DecodeZIPMember (0, raw, raw_size, size, crc ^ 1, 8) != ERR_OK);
	CHECK (DecodeZIPMember (0, payload, size, size, crc ^ 1, 0) != ERR_OK);
	CHECK (DecodeZIPMember (0, raw, raw_size, size - 1, crc, 8) != ERR_OK);
	CHECK (DecodeZIPMember (0, raw, raw_size, size + 1, crc, 8) != ERR_OK);
	CHECK (DecodeZIPMember (0, raw, raw_size + 1, size, crc, 8) != ERR_OK);
	CHECK (DecodeZIPMember (0, raw, raw_size, UINT_MAX, crc, 8) != ERR_OK);
	for (uint n = 0; n < raw_size; n++)
		CHECK (DecodeZIPMember (0, raw, n, size, crc, 8) != ERR_OK);
	const u8 empty[] = { 3, 0 };
	CHECK (DecodeZIPMember (0, empty, 2, 0, 0, 8) == ERR_OK);
	CHECK (DecodeZIPMember (0, empty, 0, 0, 0, 0) == ERR_OK);
	CHECK (DecodeZIPMember (0, 0, 0, 0, 0, 0) != ERR_OK);
	CHECK (DecodeZIPMember (0, payload, size, size, crc, 99) != ERR_OK);
	FILE *file = tmpfile ();
	CHECK (file != 0);
	if (file)
	{
		CHECK (DecodeZIPMember (file, raw, raw_size, size, crc, 8) == ERR_OK);
		rewind (file);
		u8 out[256];
		CHECK (fread (out, 1, sizeof (out), file) == size);
		CHECK (!memcmp (out, payload, size));
		fclose (file);
	}
	printf ("ZIP regressions: %d failures\n", failures);
	return failures != 0;
}
