// Layton palette/tile bounds and an independent raster fixture.
#include "lib-layton-bg.h"
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
	u8 data[150] = { 0 };
	wr_le32 (data, 3);
	wr_le16 (data + 4, 0x03e0); // transparent green
	wr_le16 (data + 6, 0x001f); // red
	wr_le16 (data + 8, 0x7c00); // blue
	wr_le32 (data + 10, 2);
	memset (data + 14, 1, 64);
	data[14] = 0;
	memset (data + 78, 2, 64);
	wr_le16 (data + 142, 2);
	wr_le16 (data + 144, 1);
	wr_le16 (data + 146, 1);
	wr_le16 (data + 148, 0);
	u8 *rgba = 0;
	uint width = 0, height = 0;
	CHECK (DecodeLaytonBG_RGBA (&rgba, &width, &height, data, sizeof (data)) == ERR_OK);
	CHECK (width == 16 && height == 8);
	if (rgba)
	{
		CHECK (!memcmp (rgba, "\0\0\xff\xff", 4));
		CHECK (!memcmp (rgba + 8 * 4, "\0\xff\0\0", 4));
		CHECK (!memcmp (rgba + 9 * 4, "\xff\0\0\xff", 4));
	}
	free (rgba);
	for (uint n = 0; n < sizeof (data); n++)
	{
		rgba = (u8 *)1;
		width = height = 999;
		CHECK (DecodeLaytonBG_RGBA (&rgba, &width, &height, data, n) != ERR_OK);
		CHECK (!rgba && !width && !height);
	}
	data[14] = 3;
	CHECK (DecodeLaytonBG_RGBA (&rgba, &width, &height, data, sizeof (data)) != ERR_OK);
	data[14] = 0;
	wr_le16 (data + 148, 2);
	CHECK (DecodeLaytonBG_RGBA (&rgba, &width, &height, data, sizeof (data)) != ERR_OK);
	wr_le16 (data + 148, 0);
	wr_le32 (data + 10, UINT_MAX);
	CHECK (DecodeLaytonBG_RGBA (&rgba, &width, &height, data, sizeof (data)) != ERR_OK);
	u8 huge[12] = { 2, 0, 0, 0, 0x10, 0, 0, 0, 0xff, 0xff, 0xff, 0xff };
	CHECK (DecodeLaytonBG_RGBA (&rgba, &width, &height, huge, sizeof (huge)) == ERR_INVALID_DATA);
	CHECK (!rgba && !width && !height);
	printf ("Layton background regressions: %d failures\n", failures);
	return failures != 0;
}
