#include "lib-bntx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Mirrors tests/test-bntx-formats.c: stub out the dclib allocator and error
// shims so the BNTX decoder links standalone.

void trace_free (ccp f, ccp p, uint l, void *v) { (void)f; (void)p; (void)l; free (v); }
void *trace_malloc (ccp f, ccp p, uint l, size_t n) { (void)f; (void)p; (void)l; return malloc (n); }
void *trace_calloc (ccp f, ccp p, uint l, size_t n, size_t s) { (void)f; (void)p; (void)l; return calloc (n, s); }
void *trace_realloc (ccp f, ccp p, uint l, void *v, size_t n) { (void)f; (void)p; (void)l; return realloc (v, n); }
void dclib_free (void *v) { free (v); }
void *dclib_malloc (size_t n) { return malloc (n); }
void *dclib_calloc (size_t n, size_t s) { return calloc (n, s); }
void *dclib_realloc (void *v, size_t n) { return realloc (v, n); }
enumError PrintError (enumError err, ccp format, ...) { (void)format; return err; }

int main (int argc, char **argv)
{
	// Proves every texture in a multi-texture container decodes, not just
	// index 0 (LegacySwitchLibraries BntxFile.Textures list). The fixture
	// is built at test time by tests/mk_bntx_extra.py (multi.bntx): two
	// BRTI entries sharing one 16x16 RGBA8 surface, so both decodes must
	// succeed with identical pixels.
	if (argc != 2)
	{
		fprintf (stderr, "usage: %s multi.bntx\n", argv[0]);
		return 2;
	}
	FILE *f = fopen (argv[1], "rb");
	assert (f);
	fseek (f, 0, SEEK_END);
	const long len = ftell (f);
	fseek (f, 0, SEEK_SET);
	assert (len > 0x40 && len < 0x1000000);
	u8 *buf = malloc ((size_t)len);
	assert (buf && fread (buf, 1, (size_t)len, f) == (size_t)len);
	fclose (f);

	bntx_t bntx;
	assert (ScanBNTX (&bntx, buf, (uint)len) == ERR_OK);
	assert (bntx.n_textures == 2);

	u8 *d0 = 0, *d1 = 0;
	uint w0 = 0, h0 = 0, w1 = 0, h1 = 0;
	assert (DecodeBNTX_RGBA (&d0, &w0, &h0, &bntx, 0) == ERR_OK && d0);
	assert (DecodeBNTX_RGBA (&d1, &w1, &h1, &bntx, 1) == ERR_OK && d1);
	assert (w0 == 16 && h0 == 16 && w1 == 16 && h1 == 16);
	assert (!memcmp (d0, d1, (size_t)16 * 16 * 4));
	// Spot-check the synthetic gradient content (mk input is solid, but the
	// encoder round-trips whatever it is; alpha must be opaque throughout).
	for (uint i = 0; i < 16 * 16; i++)
		assert (d0[i * 4 + 3] == 255);

	free (d0);
	free (d1);
	ResetBNTX (&bntx);
	free (buf);

	printf ("BNTX multi-texture decode test passed!\n");
	return 0;
}
