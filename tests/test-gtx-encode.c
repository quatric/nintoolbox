#include "lib-gtx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The project test build enables allocation tracing. Keep this standalone
// regression independent of the full CLI support graph.
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

static int check_rgba_formats (void)
{
	static const uint formats[] = { 0x01, 0x02, 0x07, 0x08, 0x0a, 0x0b, 0x0c, 0x11, 0x19, 0x1a };
	u8 rgba[7 * 5 * 4];
	for (uint i = 0; i < 7 * 5; i++)
	{
		rgba[4 * i] = (u8)(i * 7);
		rgba[4 * i + 1] = (u8)(255 - i * 3);
		rgba[4 * i + 2] = (u8)(i * 11);
		rgba[4 * i + 3] = (u8)(i * 17);
	}
	for (uint fi = 0; fi < sizeof (formats) / sizeof (*formats); fi++)
		for (uint mode = 0; mode <= 15; mode++)
		{
			u8 *file = 0, *file2 = 0, *decoded = 0;
			uint file_size = 0, file2_size = 0, w = 0, h = 0;
			if (EncodeGTX_RGBA_Format (&file, &file_size, rgba, 7, 5, formats[fi], mode))
				return 1;
			if (EncodeGTX_RGBA_Format (&file2, &file2_size, rgba, 7, 5, formats[fi], mode)
				|| file2_size != file_size || memcmp (file2, file, file_size))
				return 8;
			gtx_t g = { 0 };
			if (ScanGTX (&g, file, file_size) || DecodeGTX_RGBA (&decoded, &w, &h, &g, 0) || w != 7
				|| h != 5)
				return 2;
			ResetGTX (&g);
			free (decoded);
			free (file2);
			free (file);
		}
	return 0;
}

static int check_subresources (void)
{
	enum
	{
		W = 9,
		H = 7,
		SLICES = 3,
		SAMPLES = 4
	};
	u8 level0[W * H * SLICES * SAMPLES * 4], level1[4 * 3 * SLICES * SAMPLES * 4];
	for (uint i = 0; i < sizeof (level0); i++)
		level0[i] = (u8)(i * 13 + 5);
	for (uint i = 0; i < sizeof (level1); i++)
		level1[i] = (u8)(i * 7 + 11);
	gtx_encode_level_t levels[]
		= { { level0, sizeof (level0), SLICES }, { level1, sizeof (level1), SLICES } };
	gtx_encode_texture_t texture = { 1, W, H, SLICES, 2, 0x1a, 2, 1, 4, 0, 0, 0, 0, 0, 0, 0, 2, 0,
		SLICES, { 0, 1, 2, 3 }, levels };
	u8 *file = 0, *file2 = 0;
	uint file_size = 0, file2_size = 0;
	if (EncodeGTXTextures (&file, &file_size, &texture, 1))
		return 3;
	if (EncodeGTXTextures (&file2, &file2_size, &texture, 1) || file2_size != file_size
		|| memcmp (file2, file, file_size))
		return 9;
	gtx_t g = { 0 };
	if (ScanGTX (&g, file, file_size) || g.n_textures != 1)
		return 4;
	for (uint mip = 0; mip < 2; mip++)
		for (uint sample = 0; sample < SAMPLES; sample++)
			for (uint slice = 0; slice < SLICES; slice++)
			{
				u8 *got = 0;
				uint w = 0, h = 0;
				if (DecodeGTXSubresource_RGBA (&got, &w, &h, &g, 0, mip, slice, sample))
				{
					fprintf (stderr, "subresource decode failed: mip=%u slice=%u sample=%u\n", mip,
						slice, sample);
					return 5;
				}
				const uint mw = mip ? 4 : W, mh = mip ? 3 : H;
				const u8 *want
					= (mip ? level1 : level0) + (((sample * SLICES + slice) * mh * mw) * 4);
				if (w != mw || h != mh || memcmp (got, want, mw * mh * 4))
					return 6;
				free (got);
			}
	ResetGTX (&g);
	free (file2);
	free (file);
	return 0;
}

static int check_decode_format_matrix (void)
{
	struct fmt
	{
		uint format, bytes;
	} formats[] = { { 0x001, 1 }, { 0x101, 1 }, { 0x201, 1 }, { 0x301, 1 }, { 0x002, 1 },
		{ 0x005, 2 }, { 0x105, 2 }, { 0x205, 2 }, { 0x305, 2 }, { 0x806, 2 }, { 0x007, 2 },
		{ 0x107, 2 }, { 0x207, 2 }, { 0x307, 2 }, { 0x008, 2 }, { 0x00a, 2 }, { 0x00b, 2 },
		{ 0x00c, 2 }, { 0x10d, 4 }, { 0x30d, 4 }, { 0x80e, 4 }, { 0x00f, 4 }, { 0x10f, 4 },
		{ 0x20f, 4 }, { 0x30f, 4 }, { 0x810, 4 }, { 0x011, 4 }, { 0x111, 4 }, { 0x811, 4 },
		{ 0x816, 4 }, { 0x019, 4 }, { 0x119, 4 }, { 0x219, 4 }, { 0x319, 4 }, { 0x01a, 4 },
		{ 0x11a, 4 }, { 0x21a, 4 }, { 0x31a, 4 }, { 0x41a, 4 }, { 0x01b, 4 }, { 0x11b, 4 },
		{ 0x81c, 8 }, { 0x11c, 8 }, { 0x11d, 8 }, { 0x31d, 8 }, { 0x81e, 8 }, { 0x01f, 8 },
		{ 0x11f, 8 }, { 0x21f, 8 }, { 0x31f, 8 }, { 0x820, 8 }, { 0x122, 16 }, { 0x322, 16 },
		{ 0x823, 16 }, { 0x031, 8 }, { 0x431, 8 }, { 0x032, 16 }, { 0x432, 16 }, { 0x033, 16 },
		{ 0x433, 16 }, { 0x034, 8 }, { 0x234, 8 }, { 0x035, 16 }, { 0x235, 16 } };
	u8 raw[16] = { 0 };
	for (uint i = 0; i < sizeof (formats) / sizeof (*formats); i++)
	{
		u8 *rgba = 0;
		uint w = 0, h = 0;
		if (DecodeGX2SurfaceSlice_RGBA (&rgba, &w, &h, 1, 1, 1, 1, formats[i].format, 0, 0, 1, 0, 0,
				0, raw, formats[i].bytes)
			|| w != 1 || h != 1)
		{
			fprintf (stderr, "GX2 format decode failed: 0x%x\n", formats[i].format);
			return 7;
		}
		free (rgba);
	}
	return 0;
}

int main (void)
{
	int err = check_rgba_formats ();
	if (!err)
		err = check_subresources ();
	if (!err)
		err = check_decode_format_matrix ();
	if (err)
		fprintf (stderr, "GTX encoder test failed at stage %d\n", err);
	return err;
}
