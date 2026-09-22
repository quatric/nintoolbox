// Chunked G1TGZ framing and growth regressions.
#include "lib-g1t.h"
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
static size_t largest_allocation;
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
	if (n > largest_allocation) largest_allocation = n;
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
	if (n > largest_allocation) largest_allocation = n;
	return reject_allocation () ? 0 : realloc (v, n);
}
void dclib_free (void *v)
{
	free (v);
}
void *dclib_malloc (size_t n)
{
	if (n > largest_allocation) largest_allocation = n;
	return reject_allocation () ? 0 : malloc (n);
}
void *dclib_calloc (size_t n, size_t s)
{
	return reject_allocation () ? 0 : calloc (n, s);
}
void *dclib_realloc (void *v, size_t n)
{
	if (n > largest_allocation) largest_allocation = n;
	return reject_allocation () ? 0 : realloc (v, n);
}
char *dclib_strdup (ccp s)
{
	return s ? strdup (s) : 0;
}

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static u8 *wrap (const u8 *packed, uint packed_size, uint plain_size, bool empty_tail, uint *size)
{
	u8 empty[32];
	uLongf empty_size = sizeof empty;
	CHECK (compress (empty, &empty_size, (const u8 *)"", 0) == Z_OK);
	const uint count = empty_tail ? 2 : 1;
	*size = 12 + 4 * count + 4 + packed_size + (empty_tail ? 4 + empty_size : 0);
	u8 *out = calloc (*size, 1);
	CHECK (out != 0);
	if (!out)
		return 0;
	wr_be32 (out, 0x10000);
	wr_be32 (out + 4, count);
	wr_be32 (out + 8, plain_size);
	wr_be32 (out + 12, packed_size + 4);
	uint offset = 12 + 4 * count;
	wr_be32 (out + offset, packed_size);
	memcpy (out + offset + 4, packed, packed_size);
	if (empty_tail)
	{
		wr_be32 (out + 16, empty_size + 4);
		offset += 4 + packed_size;
		wr_be32 (out + offset, empty_size);
		memcpy (out + offset + 4, empty, empty_size);
	}
	return out;
}

static void reject (const u8 *data, uint size)
{
	u8 *out = (void *)1;
	uint out_size = 99;
	largest_allocation = 0;
	CHECK (DecodeG1TGZ (&out, &out_size, data, size) != ERR_OK);
	if (data && size >= 12)
		CHECK (largest_allocation <= rd_be32 (data + 8));
	CHECK (!out && !out_size);
}

int main (void)
{
	u8 plain[65536];
	memset (plain, 'A', sizeof plain);
	u8 packed[1024];
	uLongf packed_size = sizeof packed;
	CHECK (compress (packed, &packed_size, plain, sizeof plain) == Z_OK);
	for (uint empty_tail = 0; empty_tail < 2; empty_tail++)
	{
		uint size = 0;
		u8 *data = wrap (packed, packed_size, sizeof plain, empty_tail, &size);
		CHECK (IsG1TGZ (data, size));
		int completed = 0;
		for (long point = 0; point < 30; point++)
		{
			fail_after = point;
			allocation_failed = 0;
			u8 *out = (void *)1;
			uint out_size = 99;
			enumError err = DecodeG1TGZ (&out, &out_size, data, size);
			fail_after = -1;
			if (allocation_failed)
			{
				CHECK (err != ERR_OK);
				CHECK (!out && !out_size);
			}
			else
			{
				CHECK (err == ERR_OK && out_size == sizeof plain);
				CHECK (out && !memcmp (out, plain, sizeof plain));
				free (out);
				completed = 1;
				break;
			}
		}
		CHECK (completed);
		wr_be32 (data + 8, sizeof plain - 1);
		reject (data, size);
		wr_be32 (data + 8, sizeof plain + 1);
		reject (data, size);
		wr_be32 (data + 8, sizeof plain);
		if (!empty_tail)
		{
			CHECK (!IsG1TGZ (data, size - 1));
			reject (data, size - 1);
		}
		free (data);
	}
	reject (0, 0);

	// Retail sparse tails append zeros instead of another zlib stream.
	uint sparse_size = 0;
	u8 *sparse = wrap (packed, packed_size, sizeof plain + 16, true, &sparse_size);
	sparse_size = 24 + packed_size;
	wr_be32 (sparse + 16, 16);
	u8 *sparse_out = 0;
	uint sparse_out_size = 0;
	CHECK (DecodeG1TGZ (&sparse_out, &sparse_out_size, sparse, sparse_size) == ERR_OK);
	CHECK (sparse_out && sparse_out_size == sizeof plain + 16);
	if (sparse_out && sparse_out_size == sizeof plain + 16)
	{
		CHECK (!memcmp (sparse_out, plain, sizeof plain));
		for (uint i = sizeof plain; i < sparse_out_size; i++)
			CHECK (sparse_out[i] == 0);
	}
	free (sparse_out);
	wr_be32 (sparse + 16, 0x10000000u);
	reject (sparse, sparse_size);
	free (sparse);

	// A single stream may legitimately expand beyond the old cumulative
	// growth limit (roughly 128 MiB) while remaining below NFMT_MAX_OUTPUT.
	const uint large_size = 130u << 20;
	u8 *large = calloc (large_size, 1);
	u8 *encoded = malloc (1u << 20);
	CHECK (large && encoded);
	if (large && encoded)
	{
		uLongf encoded_size = 1u << 20;
		CHECK (compress (encoded, &encoded_size, large, large_size) == Z_OK);
		free (large);
		large = 0;
		uint size = 0;
		u8 *data = wrap (encoded, encoded_size, large_size, false, &size);
		u8 *out = 0;
		uint out_size = 0;
		CHECK (DecodeG1TGZ (&out, &out_size, data, size) == ERR_OK);
		CHECK (out && out_size == large_size);
		if (out && out_size == large_size)
			for (uint i = 0; i < out_size; i++)
				if (out[i]) { CHECK (false); break; }
		free (out);
		free (data);
	}
	free (large);
	free (encoded);
	printf ("G1TGZ regressions: %d failures\n", failures);
	return failures != 0;
}
