// Run with: make -C project test-brstm-bounds
#include "lib-brstm.h"
static int fail_after = -1;
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
	return fail_after == 0 ? 0 : (fail_after > 0 ? fail_after--, malloc (n) : malloc (n));
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
	return fail_after == 0 ? 0 : (fail_after > 0 ? fail_after--, malloc (n) : malloc (n));
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

enumError PrintError (
	ccp func, ccp file, unsigned int line, int syserr, enumError err_code, ccp format, ...)
{
	(void)func;
	(void)file;
	(void)line;
	(void)syserr;
	(void)format;
	return err_code;
}

static void put16 (u8 *p, unsigned v, int le)
{
	p[le ? 0 : 1] = v;
	p[le ? 1 : 0] = v >> 8;
}
static void put32 (u8 *p, u32 v, int le)
{
	for (int i = 0; i < 4; i++)
		p[le ? i : 3 - i] = v >> (8 * i);
}

// Independent two-channel/two-block fixture. The final block has a shorter
// stride and padding between channels, which single-block roundtrips miss.
static size_t fixture (u8 *data, int variant, int codec)
{
	const int le = variant == BRSTM_VARIANT_CSTM;
	const int rstm = variant == BRSTM_VARIANT_RSTM;
	const unsigned block = codec == 0 ? 4 : codec == 1 ? 8 : 16;
	const unsigned used = codec == 0 ? 1 : 2;
	const unsigned span = codec == 0 ? 2 : codec == 1 ? 4 : 8;
	memset (data, 0, 512);
	memcpy (data, rstm ? "RSTM" : le ? "CSTM" : "FSTM", 4);
	put16 (data + 4, 0xfeff, le);
	if (rstm)
	{
		put32 (data + 0x10, 0x40, le);
		put32 (data + 0x20, 0x160, le);
	}
	else
	{
		put16 (data + 16, 2, le);
		put16 (data + 20, 0x4000, le);
		put32 (data + 24, 0x40, le);
		put16 (data + 32, 0x4002, le);
		put32 (data + 36, 0x160, le);
	}
	memcpy (data + 0x40, rstm ? "HEAD" : "INFO", 4);
	memcpy (data + 0x160, "DATA", 4);
	put32 (data + 0x4c, 0x18, le);
	put32 (data + 0x5c, 0x68, le); // channel table at 0xb0
	u8 *h = data + 0x60;
	h[0] = codec;
	h[2] = 2;
	if (rstm)
		put16 (h + 4, 32000, le);
	else
		put32 (h + 4, 32000, le);
	put32 (h + 12, codec == 2 ? 29 : 5, le);
	unsigned base = rstm ? 0x14 : 0x10;
	put32 (h + base, 2, le);
	put32 (h + base + 4, block, le);
	put32 (h + base + 12, used, le);
	put32 (h + base + 20, span, le);
	if (rstm)
	{
		put32 (data + 0xb8, 0x90, le);
		put32 (data + 0xc0, 0xc0, le);
	}
	for (int ch = 0; ch < 2; ch++)
	{
		u8 *first = data + 0x180 + ch * block;
		u8 *last = data + 0x180 + 2 * block + ch * span;
		if (codec == 2)
		{
			for (unsigned f = 0; f < block; f += 8)
				memset (first + f + 1, ch ? 0x22 : 0x11, 7);
			last[1] = ch ? 0x40 : 0x30;
		}
		else
		{
			for (int i = 0; i < 4; i++)
			{
				int sample = ch ? -(i + 1) : i + 1;
				if (codec == 0)
					first[i] = sample;
				else
					put16 (first + 2 * i, sample, le);
			}
			if (codec == 0)
				last[0] = ch ? -5 : 5;
			else
				put16 (last, ch ? -5 : 5, le);
		}
	}
	return 0x180 + 2 * block + span + used;
}

static void invalid (const u8 *data, size_t size)
{
	brstm_audio_t audio;
	CHECK (DecodeBRSTM (&audio, data, size) != ERR_OK);
	for (int ch = 0; ch < DSP_ADPCM_MAX_CHANNELS; ch++)
		CHECK (!audio.pcm[ch]);
	FreeBRSTMAudio (&audio);
}

static void test_fixture (int variant, int codec)
{
	u8 data[512], saved[512];
	size_t size = fixture (data, variant, codec);
	brstm_audio_t audio;
	CHECK (DecodeBRSTM (&audio, data, size) == ERR_OK);
	CHECK (audio.channels == 2 && audio.sample_rate == 32000);
	CHECK (audio.n_samples == (codec == 2 ? 29 : 5));
	for (int ch = 0; ch < 2; ch++)
	{
		CHECK (audio.pcm[ch]);
		if (!audio.pcm[ch])
			continue;
		for (int i = 0; i < audio.n_samples; i++)
		{
			int want = codec == 2 ? (i == 28 ? ch + 3 : ch + 1)
								  : (ch ? -(i + 1) : i + 1) * (codec == 0 ? 256 : 1);
			CHECK (audio.pcm[ch][i] == want);
		}
	}
	FreeBRSTMAudio (&audio);
	memcpy (saved, data, sizeof data);
	// Give ASan an allocation of exactly the advertised length.
	for (size_t n = 0; n < size; n++)
	{
		u8 *short_data = malloc (n ? n : 1);
		memcpy (short_data, saved, n);
		invalid (short_data, n);
		free (short_data);
	}
	const int le = variant == BRSTM_VARIANT_CSTM;
	unsigned block = variant == BRSTM_VARIANT_RSTM ? 0x74 : 0x70;
	const unsigned offsets[] = { 0x4c, 0x6c, block, block + 4, block + 12, block + 20 };
	for (unsigned i = 0; i < sizeof offsets / sizeof *offsets; i++)
	{
		memcpy (data, saved, sizeof data);
		put32 (data + offsets[i], UINT32_MAX, le);
		invalid (data, size);
	}
	if (codec == 2)
	{
		memcpy (data, saved, sizeof data);
		put32 (data + 0x5c, UINT32_MAX, le);
		invalid (data, size);
	}
	const unsigned zero_fields[] = { block, block + 4, 0x64 };
	for (unsigned i = 0; i < sizeof zero_fields / sizeof *zero_fields; i++)
	{
		memcpy (data, saved, sizeof data);
		if (zero_fields[i] == 0x64 && variant == BRSTM_VARIANT_RSTM)
			put16 (data + 0x64, 0, le);
		else
			put32 (data + zero_fields[i], 0, le);
		invalid (data, size);
	}
	if (variant != BRSTM_VARIANT_RSTM)
	{
		memcpy (data, saved, sizeof data);
		put32 (data + 0x64, UINT32_MAX, le);
		invalid (data, size);
	}
	for (int n = 0; n < 3; n++)
	{
		fail_after = n;
		CHECK (DecodeBRSTM (&audio, saved, size) == ERR_OUT_OF_MEMORY);
		fail_after = -1;
		for (int ch = 0; ch < DSP_ADPCM_MAX_CHANNELS; ch++)
			CHECK (!audio.pcm[ch]);
		FreeBRSTMAudio (&audio);
	}
	memcpy (data, saved, sizeof data);
	memset (data + 4, 0, 2);
	invalid (data, size);
}

static void test_roundtrip (int variant, bool adpcm)
{
	s16 samples[31];
	for (int i = 0; i < 31; i++)
		samples[i] = (i - 15) * 100;
	brstm_audio_t source = { 0 }, decoded;
	source.variant = variant;
	source.channels = 1;
	source.sample_rate = 32000;
	source.n_samples = 31;
	source.pcm[0] = samples;
	u8 *data = 0;
	size_t size = 0;
	CHECK (EncodeBRSTM (&data, &size, &source, adpcm) == ERR_OK);
	CHECK (DecodeBRSTM (&decoded, data, size) == ERR_OK);
	CHECK (decoded.n_samples == 31 && decoded.channels == 1);
	if (!adpcm && decoded.pcm[0])
		CHECK (!memcmp (samples, decoded.pcm[0], sizeof samples));
	FreeBRSTMAudio (&decoded);
	free (data);
}

int main (void)
{
	invalid (0, 512);
	CHECK (DecodeBRSTM (0, 0, 0) == ERR_INVALID_DATA);
	for (int variant = 0; variant < 3; variant++)
	{
		for (int codec = 0; codec < 3; codec++)
			test_fixture (variant, codec);
		test_roundtrip (variant, false);
		test_roundtrip (variant, true);
	}
	printf ("BRSTM regressions: %d failures\n", failures);
	return failures != 0;
}
