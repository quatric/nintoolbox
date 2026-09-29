// SCB companion bounds and transparent palette entries.
#include "lib-luminous-bg.h"
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
	u8 map[18] = { 0 }, tiles[64] = { 0 }, palette[512] = { 0 };
	wr_le32 (map, 1);
	wr_le32 (map + 4, 1);
	wr_le32 (map + 8, 16);
	wr_le32 (map + 12, 2);
	tiles[0] = 1;
	wr_le16 (palette + 2, 31);
	u8 *rgba = 0;
	uint w = 0, h = 0;
	CHECK (DecodeLuminousBG_RGBA (&rgba, &w, &h, map, 18, tiles, 64, palette, 512) == ERR_OK);
	CHECK (w == 8 && h == 8);
	if (rgba)
	{
		CHECK (rgba[0] == 255 && rgba[3] == 255 && rgba[7] == 0);
	}
	free (rgba);
	for (uint n = 0; n < 18; n++)
	{
		CHECK (DecodeLuminousBG_RGBA (&rgba, &w, &h, map, n, tiles, 64, palette, 512) != ERR_OK);
		CHECK (!rgba && !w && !h);
	}
	for (uint n = 0; n < 64; n++)
	{
		CHECK (DecodeLuminousBG_RGBA (&rgba, &w, &h, map, 18, tiles, n, palette, 512) != ERR_OK);
		CHECK (!rgba && !w && !h);
	}
	wr_le16 (map + 16, 1);
	CHECK (DecodeLuminousBG_RGBA (&rgba, &w, &h, map, 18, tiles, 64, palette, 512) != ERR_OK);
	wr_le16 (map + 16, 0x1000);
	CHECK (DecodeLuminousBG_RGBA (&rgba, &w, &h, map, 18, tiles, 64, palette, 512) != ERR_OK);
	wr_le16 (map + 16, 0);
	wr_le32 (map, UINT_MAX);
	CHECK (DecodeLuminousBG_RGBA (&rgba, &w, &h, map, 18, tiles, 64, palette, 512) != ERR_OK);
	printf ("Luminous background regressions: %d failures\n", failures);
	return failures != 0;
}
