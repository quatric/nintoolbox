// Shared zlib decoder growth and truncation regressions.
#include "lib-szs.h"
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
static size_t last_allocation;
static int simulate_full_buffers;
static uint reallocations;
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
	last_allocation = n;
	return reject_allocation () ? 0 : malloc (simulate_full_buffers ? 1 : n);
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
	last_allocation = n;
	reallocations++;
	return reject_allocation () ? 0 : realloc (v, simulate_full_buffers ? 1 : n);
}
void dclib_free (void *v)
{
	free (v);
}
void *dclib_malloc (size_t n)
{
	last_allocation = n;
	return reject_allocation () ? 0 : malloc (simulate_full_buffers ? 1 : n);
}
void *dclib_calloc (size_t n, size_t s)
{
	return reject_allocation () ? 0 : calloc (n, s);
}
void *dclib_realloc (void *v, size_t n)
{
	last_allocation = n;
	reallocations++;
	return reject_allocation () ? 0 : realloc (v, simulate_full_buffers ? 1 : n);
}
char *dclib_strdup (ccp s)
{
	return s ? strdup (s) : 0;
}

// A full-buffer response exercises the capacity ceiling without allocating
// hundreds of megabytes. Normal tests still use the real zlib implementation.
#undef inflate
extern int inflate (z_streamp stream, int flush);
int zlib_test_inflate (z_streamp stream, int flush)
{
	if (simulate_full_buffers)
	{
		stream->avail_out = 0;
		return Z_BUF_ERROR;
	}
	return inflate (stream, flush);
}

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

int main (void)
{
	// Stop at the first allocation to inspect capacity arithmetic without
	// needing a huge source buffer or allowing inflate to read the sentinel.
	const u8 sentinel = 0;
	const uint large_sizes[] = {0x3ffffc00u, 0x40000000u, UINT_MAX};
	for (uint i = 0; i < sizeof large_sizes / sizeof *large_sizes; i++)
	{
		u8 *out = (void *)1;
		uint size = 99;
		fail_after = 0;
		CHECK (DecodeZlibGrow (&out, &size, &sentinel, large_sizes[i]) == ERR_OUT_OF_MEMORY);
		CHECK (!out && !size);
		CHECK (last_allocation == (256u << 20));
	}
	fail_after = -1;
	simulate_full_buffers = 1;
	u8 *limited = (void *)1;
	uint limited_size = 99;
	reallocations = 0;
	CHECK (DecodeZlibGrow (&limited, &limited_size, &sentinel, 48u << 20) == ERR_FILE_TOO_BIG);
	CHECK (!limited && !limited_size);
	CHECK (reallocations == 1 && last_allocation == (256u << 20));
	simulate_full_buffers = 0;
	for (uint null_src = 0; null_src < 2; null_src++)
	{
		u8 *out = (void *)1;
		uint size = 99;
		CHECK (DecodeZlibGrow (&out, &size, null_src ? 0 : &sentinel, 0) != ERR_OK);
		CHECK (!out && !size);
	}

	u8 plain[65536];
	memset (plain, 'A', sizeof plain);
	for (int raw = 0; raw <= 1; raw++)
	{
		u8 encoded[1024];
		z_stream stream = {0};
		CHECK (deflateInit2 (&stream, 9, Z_DEFLATED, raw ? -15 : 15, 8, Z_DEFAULT_STRATEGY) == Z_OK);
		stream.next_in = plain;
		stream.avail_in = sizeof plain;
		stream.next_out = encoded;
		stream.avail_out = sizeof encoded;
		CHECK (deflate (&stream, Z_FINISH) == Z_STREAM_END);
		uint encoded_size = stream.total_out;
		deflateEnd (&stream);

		u8 *out = 0;
		uint size = 0;
		reallocations = 0;
		CHECK (DecodeZlibGrow (&out, &size, encoded, encoded_size) == ERR_OK);
		CHECK (size == sizeof plain && out && !memcmp (out, plain, sizeof plain));
		CHECK (reallocations > 0);
		free (out);

		// Every incomplete prefix must fail with cleared outputs. Short
		// prefixes should not allocate larger buffers when input runs out.
		for (uint prefix = 1; prefix < encoded_size; prefix++)
		{
			out = (void *)1;
			size = 99;
			reallocations = 0;
			CHECK (DecodeZlibGrow (&out, &size, encoded, prefix) == ERR_INVALID_DATA);
			CHECK (!out && !size);
			if (prefix == 1)
				CHECK (!reallocations);
		}

		// Fail the initial allocation and each growth allocation in turn.
		int completed = 0;
		for (long point = 0; point < 30; point++)
		{
			fail_after = point;
			allocation_failed = 0;
			out = (void *)1;
			size = 99;
			enumError err = DecodeZlibGrow (&out, &size, encoded, encoded_size);
			fail_after = -1;
			if (allocation_failed)
			{
				CHECK (err == ERR_OUT_OF_MEMORY);
				CHECK (!out && !size);
			}
			else
			{
				CHECK (err == ERR_OK && size == sizeof plain);
				free (out);
				completed = 1;
				break;
			}
		}
		CHECK (completed);
	}

	// FZIP encode/decode roundtrip
	u8 *fzip_out = 0;
	uint fzip_out_sz = 0;
	CHECK (EncodeFZIP (&fzip_out, &fzip_out_sz, plain, sizeof plain) == ERR_OK);
	CHECK (fzip_out && fzip_out_sz > 8 && !memcmp (fzip_out, "FZIP", 4));
	u8 *fzip_dec = 0;
	uint fzip_dec_sz = 0;
	CHECK (DecodeFZIP (&fzip_dec, &fzip_dec_sz, fzip_out, fzip_out_sz) == ERR_OK);
	CHECK (fzip_dec && fzip_dec_sz == sizeof plain && !memcmp (fzip_dec, plain, sizeof plain));
	free (fzip_dec);
	free (fzip_out);

	printf ("Zlib growth regressions: %d failures\n", failures);
	return failures != 0;
}
