// Run with: make -C project test-compression-bounds
#include "lib-nintendo.h"
#include "lib-huff.h"
#include "lib-yay0.h"
#include "lib-lz10.h"
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

static void test_diff16 (void)
{
	const u8 input[] = { 0xa1, 0xff, 0x7c, 0x10, 0xfe };
	for (uint n = 1; n <= sizeof (input); n++)
	{
		u8 *enc = 0, *dec = 0;
		uint es = 0, ds = 0;
		CHECK (EncodeDiff16 (&enc, &es, input, n) == ERR_OK);
		CHECK (DecodeDiff16 (&dec, &ds, enc, es) == ERR_OK);
		CHECK (ds == n && dec && !memcmp (input, dec, n));
		free (enc);
		free (dec);
	}
	// Independent little-endian deltas: 0xffff, then +0x007d.
	const u8 encoded[] = { 0x81, 3, 0, 0, 0xff, 0xff, 0x7d, 0 };
	const u8 expected[] = { 0xff, 0xff, 0x7c };
	u8 *out = 0;
	uint size = 0;
	CHECK (DecodeDiff16 (&out, &size, encoded, sizeof (encoded)) == ERR_OK);
	CHECK (out && size == sizeof (expected) && !memcmp (out, expected, size));
	free (out);
}

static void test_sszl (void)
{
	// The declared compressed size must not wrap when adding the header.
	const u8 overflow[] = { 'S', 'S', 'Z', 'L', 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 1, 0, 0, 0 };
	u8 *out = 0;
	uint size = 0;
	CHECK (DecodeSSZL (&out, &size, overflow, sizeof (overflow)) != ERR_OK);
	free (out);
	const u8 valid[] = { 'S', 'S', 'Z', 'L', 0, 0, 0, 0, 4, 0, 0, 0, 3, 0, 0, 0, 7, 'A', 'B', 'C' };
	out = 0;
	CHECK (DecodeSSZL (&out, &size, valid, sizeof (valid)) == ERR_OK);
	CHECK (out && size == 3 && !memcmp (out, "ABC", 3));
	free (out);
	for (uint n = 0; n < sizeof (valid); n++)
	{
		out = 0;
		CHECK (DecodeSSZL (&out, &size, valid, n) != ERR_OK);
		free (out);
	}
}

static void test_vlx (void)
{
	// Two 1-bit length codes: 0 = literal, 1 = 2-bit match length.
	// Distance code 0 uses 1 extra bit. Payload: literal A, length 4,
	// distance 1 => AAAAA. All fields are LSB-first.
	const u8 mixed[] = { 1, 5, 0x21, 2, 0, 3, 0x20, 2, 0x10, 0x82, 2 };
	u8 *out = 0;
	uint size = 0;
	CHECK (CxIsCompressedVlx (mixed, sizeof (mixed)));
	CHECK (DecodeVLX (&out, &size, mixed, sizeof (mixed)) == ERR_OK);
	CHECK (out && size == 5 && !memcmp (out, "AAAAA", 5));
	free (out);
	// An 11-bit code selects a 15-bit match length. Refilling with pending
	// bits must preserve the following literal tokens across word boundaries.
	const u8 wide[] = { 2, 7, 0x80, 0x21, 2, 0, 1, 0xf8, 2, 0x10, 0x82, 0x02, 0x00, 0x00, 0x80,
		0x90, 0x21, 0x44, 0x8a, 0x18, 0x39, 0x02 };
	out = 0;
	CHECK (DecodeVLX (&out, &size, wide, sizeof (wide)) == ERR_OK);
	CHECK (out && size == 32775);
	if (out && size == 32775)
	{
		for (uint i = 0; i < 32769; i++)
			CHECK (out[i] == 'A');
		CHECK (!memcmp (out + 32769, "BCDEFG", 6));
	}
	free (out);
	// A one-bit literal marker is present, but only seven literal bits.
	const u8 truncated[] = { 1, 1, 0x10, 2, 0, 0 };
	out = 0;
	CHECK (!CxIsCompressedVlx (truncated, sizeof (truncated)));
	CHECK (DecodeVLX (&out, &size, truncated, sizeof (truncated)) != ERR_OK);
	free (out);
	u8 input[257];
	for (uint i = 0; i < sizeof (input); i++)
		input[i] = (u8)(i * 37 + 0xa1);
	for (uint n = 1; n <= sizeof (input); n++)
	{
		u8 *enc = 0;
		uint es = 0;
		out = 0;
		CHECK (EncodeVLX (&enc, &es, input, n) == ERR_OK);
		CHECK (DecodeVLX (&out, &size, enc, es) == ERR_OK);
		CHECK (out && size == n && !memcmp (out, input, n));
		free (out);
		out = 0;
		CHECK (DecodeVLX (&out, &size, enc, es - 1) != ERR_OK);
		free (out);
		free (enc);
	}
}

static void test_overlay (void)
{
	// BLZ trailer counts the entire compressed span, including itself.
	// Read backward: three literals C,B,A, then an 18-byte distance-3 match.
	const u8 valid[] = { 0, 0xf0, 'A', 'B', 'C', 0x10, 14, 0, 0, 8, 7, 0, 0, 0 };
	const char expected[] = "ABCABCABCABCABCABCABC";
	u8 *out = 0;
	uint size = 0;
	CHECK (CxIsCompressedLZOvl (valid, sizeof (valid)));
	CHECK (DecodeLZOvl (&out, &size, valid, sizeof (valid)) == ERR_OK);
	CHECK (out && size == 21 && !memcmp (out, expected, 21));
	free (out);
	// Older decoding accepted exhausted input without filling its output.
	const u8 exhausted[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 8, 1, 0, 0, 0 };
	out = 0;
	CHECK (DecodeLZOvl (&out, &size, exhausted, sizeof (exhausted)) != ERR_OK);
	free (out);

	// Header padding is excluded from the token stream but included in
	// the compressed span and the total stored size.
	for (uint padding = 1; padding <= 3; padding++)
	{
		u8 padded[sizeof (valid) + 3];
		memcpy (padded, valid, 6);
		memset (padded + 6, 0xff, padding);
		memcpy (padded + 6 + padding, valid + 6, 8);
		padded[6 + padding] += padding;
		padded[9 + padding] += padding;
		padded[10 + padding] -= padding;
		out = 0;
		CHECK (CxIsCompressedLZOvl (padded, sizeof (valid) + padding));
		CHECK (DecodeLZOvl (&out, &size, padded, sizeof (valid) + padding) == ERR_OK);
		CHECK (out && size == 21 && !memcmp (out, expected, 21));
		free (out);
	}
	u8 prefixed[sizeof (valid) + 3];
	memcpy (prefixed, "xyz", 3);
	memcpy (prefixed + 3, valid, sizeof (valid));
	out = 0;
	CHECK (DecodeLZOvl (&out, &size, prefixed, sizeof (prefixed)) == ERR_OK);
	CHECK (out && size == 24 && !memcmp (out, "xyz", 3) && !memcmp (out + 3, expected, 21));
	free (out);
	for (uint n = 1; n <= 3; n++)
	{
		u8 *enc = 0;
		uint es = 0;
		out = 0;
		CHECK (EncodeLZOvl (&enc, &es, (const u8 *)"ABC", n) == ERR_OK);
		CHECK (DecodeLZOvl (&out, &size, enc, es) == ERR_OK);
		CHECK (out && size == n && !memcmp (out, "ABC", n));
		free (out);
		free (enc);
	}
	// Truncated literals and invalid matches used to return allocated junk.
	u8 malformed[sizeof (valid)];
	memcpy (malformed, valid, sizeof (valid));
	malformed[5] = 0;
	out = 0;
	CHECK (DecodeLZOvl (&out, &size, malformed, sizeof (malformed)) != ERR_OK);
	free (out);
	memcpy (malformed, valid, sizeof (valid));
	malformed[5] = 0x80;
	out = 0;
	CHECK (DecodeLZOvl (&out, &size, malformed, sizeof (malformed)) != ERR_OK);
	free (out);
	memcpy (malformed, valid, sizeof (valid));
	memset (malformed + 10, 0xff, 4);
	out = 0;
	CHECK (DecodeLZOvl (&out, &size, malformed, sizeof (malformed)) != ERR_OK);
	free (out);
}

static void test_huffman (void)
{
	// BIOS-compatible trees include their size byte in the paired layout.
	// Codes 01011001 decode to ABABBAAB; four-bit symbols pack low first.
	const u8 h8[] = { 0x28, 8, 0, 0, 1, 0xc0, 'A', 'B', 0, 0, 0, 0x59 };
	const u8 h4[] = { 0x24, 4, 0, 0, 1, 0xc0, 1, 2, 0, 0, 0, 0x59 };
	const u8 want4[] = { 0x21, 0x21, 0x12, 0x21 };
	u8 *out = 0;
	uint size = 0;
	CHECK (DecodeNintendoHuff (&out, &size, h8, sizeof (h8)) == ERR_OK);
	CHECK (out && size == 8 && !memcmp (out, "ABABBAAB", 8));
	free (out);
	out = 0;
	CHECK (DecodeNintendoHuff (&out, &size, h4, sizeof (h4)) == ERR_OK);
	CHECK (out && size == 4 && !memcmp (out, want4, 4));
	free (out);
	const u8 nested[] = { 0x28, 4, 0, 0, 2, 0x80, 'A', 0xc0, 'B', 'C', 0, 0, 0, 0x58 };
	out = 0;
	CHECK (DecodeNintendoHuff (&out, &size, nested, sizeof (nested)) == ERR_OK);
	CHECK (out && size == 4 && !memcmp (out, "ABCA", 4));
	free (out);
	const u8 extended[] = { 0x28, 0, 0, 0, 8, 0, 0, 0, 1, 0xc0, 'A', 'B', 0, 0, 0, 0x59 };
	out = 0;
	CHECK (DecodeNintendoHuff (&out, &size, extended, sizeof (extended)) == ERR_OK);
	CHECK (out && size == 8 && !memcmp (out, "ABABBAAB", 8));
	free (out);
	// Single-symbol trees make the output independently predictable.
	for (uint four = 0; four < 2; four++)
	{
		const u8 input[] = { 0xaa, 0xaa, 0xaa, 0xaa };
		u8 *encoded = 0;
		uint es = 0;
		CHECK (EncodeNintendoHuff (&encoded, &es, input, sizeof (input), four) == ERR_OK);
		CHECK (encoded && es == 12 && encoded[4] == 1 && encoded[5] == 0xc0);
		if (encoded && es == 12)
		{
			CHECK (encoded[6] == (four ? 0xb : 0xab));
			CHECK (encoded[7] == (four ? 0xa : 0xaa));
			CHECK (encoded[8] == 0 && encoded[9] == 0 && encoded[10] == 0);
			CHECK (encoded[11] == (four ? 0xff : 0xf0));
		}
		free (encoded);
		u8 data[129];
		for (uint i = 0; i < sizeof (data); i++)
			data[i] = (u8)((i * 7) % 23);
		for (uint n = 1; n <= sizeof (data); n++)
		{
			encoded = out = 0;
			CHECK (EncodeNintendoHuff (&encoded, &es, data, n, four) == ERR_OK);
			CHECK (DecodeNintendoHuff (&out, &size, encoded, es) == ERR_OK);
			CHECK (out && size == n && !memcmp (out, data, n));
			free (out);
			free (encoded);
		}
	}
}

static void test_huffman_bounds (void)
{
	// Legacy-shaped input previously read a second word beyond this array.
	const u8 truncated[] = { 0x28, 33, 0, 0, 1, 0xc0, 0, 'A', 'B', 0, 0, 0, 0 };
	u8 *out = 0;
	uint size = 0;
	CHECK (DecodeNintendoHuff (&out, &size, truncated, sizeof (truncated)) != ERR_OK);
	CHECK (!out && !size);
	free (out);
	// Every partial final word must be rejected, with output state reset.
	const u8 two_words[] = { 0x28, 33, 0, 0, 1, 0xc0, 'A', 'B', 0, 0, 0, 0, 0, 0, 0, 0 };
	for (uint n = 0; n < sizeof (two_words); n++)
	{
		out = 0;
		size = 0;
		CHECK (DecodeNintendoHuff (&out, &size, two_words, n) != ERR_OK);
		CHECK (!out && !size);
		free (out);
	}
	out = 0;
	CHECK (DecodeNintendoHuff (&out, &size, two_words, sizeof (two_words)) == ERR_OK);
	CHECK (out && size == 33);
	if (out && size == 33)
		for (uint i = 0; i < size; i++)
			CHECK (out[i] == 'A');
	free (out);
	const u8 invalid_tree[] = { 0x28, 1, 0, 0, 1, 0xff, 'A', 'B', 0, 0, 0, 0 };
	out = 0;
	size = 0;
	CHECK (DecodeNintendoHuff (&out, &size, invalid_tree, sizeof (invalid_tree)) != ERR_OK);
	CHECK (!out && !size);
	free (out);
}

static void test_yay0 (void)
{
	const u8 valid[] = { 'Y', 'a', 'y', '0', 0, 0, 0, 6, 0, 0, 0, 20, 0, 0, 0, 22, 0xe0, 0, 0, 0,
		0x10, 2, 'A', 'B', 'C' };
	u8 *out = 0;
	uint size = 0;
	CHECK (DecodeYay0 (&out, &size, valid, sizeof (valid)) == ERR_OK);
	CHECK (out && size == 6 && !memcmp (out, "ABCABC", 6));
	free (out);
	u8 malformed[sizeof (valid)];
	memcpy (malformed, valid, sizeof (valid));
	memset (malformed + 8, 0xff, 4);
	out = 0;
	CHECK (DecodeYay0 (&out, &size, malformed, sizeof (malformed)) != ERR_OK);
	free (out);
	const u8 overlapping[] = { 'Y', 'a', 'y', '0', 0, 0, 0, 3, 0, 0, 0, 16, 0, 0, 0, 20, 0xe0, 0, 0,
		0, 'A', 'B', 'C' };
	out = 0;
	CHECK (DecodeYay0 (&out, &size, overlapping, sizeof (overlapping)) != ERR_OK);
	free (out);
	// Chunks cannot point into the header, and links cannot consume chunks.
	memcpy (malformed, valid, sizeof (valid));
	malformed[15] = 0;
	out = 0;
	CHECK (DecodeYay0 (&out, &size, malformed, sizeof (malformed)) != ERR_OK);
	free (out);
	memcpy (malformed, valid, sizeof (valid));
	malformed[15] = 21;
	out = 0;
	CHECK (DecodeYay0 (&out, &size, malformed, sizeof (malformed)) != ERR_OK);
	free (out);
	for (uint n = 0; n < sizeof (valid); n++)
	{
		out = 0;
		CHECK (DecodeYay0 (&out, &size, valid, n) != ERR_OK);
		free (out);
	}
	u8 input[129];
	for (uint i = 0; i < sizeof (input); i++)
		input[i] = (u8)(i % 5);
	for (uint n = 1; n <= sizeof (input); n++)
	{
		u8 *enc = 0;
		uint es = 0;
		out = 0;
		CHECK (EncodeYay0 (&enc, &es, input, n) == ERR_OK);
		CHECK (DecodeYay0 (&out, &size, enc, es) == ERR_OK);
		CHECK (out && size == n && !memcmp (out, input, n));
		free (out);
		free (enc);
	}
}

static void test_lz10raw (void)
{
	const u8 input[] = { 1, 2, 3 };
	uint size = 0;
	u8 *out = 0;
	CHECK (EncodeLZ10Raw (0, &size, input, sizeof (input)) != ERR_OK);
	CHECK (EncodeLZ10Raw (&out, 0, input, sizeof (input)) != ERR_OK);
	free (out);
	out = 0;
	CHECK (EncodeLZ10Raw (&out, &size, input, sizeof (input)) == ERR_OK);
	CHECK (out && size == 4 && out[0] == 0 && !memcmp (out + 1, input, 3));
	free (out);
}

int main (int argc, char **argv)
{
	if (argc == 1 || !strcmp (argv[1], "huffman"))
		test_huffman ();
	if (argc == 1 || !strcmp (argv[1], "huffman_bounds"))
		test_huffman_bounds ();
	if (argc == 1 || !strcmp (argv[1], "yay0"))
		test_yay0 ();
	if (argc == 1 || !strcmp (argv[1], "lz10raw"))
		test_lz10raw ();
	if (argc == 1 || !strcmp (argv[1], "diff16"))
		test_diff16 ();
	if (argc == 1 || !strcmp (argv[1], "vlx"))
		test_vlx ();
	if (argc == 1 || !strcmp (argv[1], "overlay"))
		test_overlay ();
	if (argc == 1 || !strcmp (argv[1], "sszl"))
		test_sszl ();
	fprintf (stderr, "%d compression regression failures\n", failures);
	return failures != 0;
}
