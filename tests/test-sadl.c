// SADL bounded input and independent stereo codec vectors.
#include "lib-nintendo.h"
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

int main (void)
{
	u8 data[0x140] = { 0 };
	memcpy (data, "sadl", 4);
	data[0x32] = 2;
	data[0x33] = 0x72;
	wr_le32 (data + 0x40, sizeof (data));
	wr_le32 (data + 0x48, 0x100);
	wr_le16 (data + 0x80, 1000);
	wr_le16 (data + 0x84, (u16)-1000);
	for (uint block = 0; block < 2; block++)
	{
		memset (data + 0x100 + block * 32, 0x11, 16);
		memset (data + 0x110 + block * 32, 0x99, 16);
	}
	u8 *wav = 0;
	uint size = 0;
	CHECK (DecodeSADL_WAV (&wav, &size, data, sizeof (data)) == ERR_OK);
	CHECK (size == 44 + 64 * 4);
	if (wav)
		for (uint i = 0; i < 64; i++)
		{
			CHECK ((s16)rd_le16 (wav + 44 + i * 4) == 1001 + (int)i);
			CHECK ((s16)rd_le16 (wav + 46 + i * 4) == -1001 - (int)i);
		}
	free (wav);
	for (uint n = 0; n < sizeof (data); n++)
	{
		wav = (u8 *)1;
		size = 999;
		CHECK (DecodeSADL_WAV (&wav, &size, data, n) != ERR_OK);
		CHECK (!wav && !size);
	}
	data[0x33] = 0xb2;
	for (uint block = 0; block < 2; block++)
	{
		memset (data + 0x100 + block * 32, 0x91, 15);
		data[0x10f + block * 32] = 0x86;
		memset (data + 0x110 + block * 32, 0x6e, 15);
		data[0x11f + block * 32] = 0x86;
	}
	CHECK (DecodeSADL_WAV (&wav, &size, data, sizeof (data)) == ERR_OK);
	CHECK (size == 44 + 60 * 4);
	if (wav)
		for (uint i = 0; i < 60; i++)
		{
			CHECK ((s16)rd_le16 (wav + 44 + i * 4) == 64);
			CHECK ((s16)rd_le16 (wav + 46 + i * 4) == -64);
		}
	free (wav);
	// Exercise every predictor/scale combination without signed-shift UB.
	for (uint header = 0; header < 256; header++)
	{
		data[0x10f] = header;
		enumError err = DecodeSADL_WAV (&wav, &size, data, sizeof (data));
		CHECK (err == ERR_OK || err == ERR_INVALID_DATA);
		free (wav);
	}
	// Reject expansion sizes before narrowing a sample count or reading payloads.
	data[0x32] = 1;
	data[0x33] = 0x72;
	wr_le32 (data + 0x40, 0x80000110u);
	CHECK (DecodeSADL_WAV (&wav, &size, data, UINT_MAX) == ERR_FILE_TOO_BIG);
	CHECK (!wav && !size);
	CHECK (DecodeSADL_WAV (&wav, &size, 0, 0) == ERR_INVALID_DATA);
	printf ("SADL regressions: %d failures\n", failures);
	return failures != 0;
}
