// Round trip an indexed BRRES texture through SaveTEXwithPLT0().
//
// Nearly every texture shipped in a real BRRES is indexed -- across the
// fixtures in tests/fixtures 154 of 184 are C4 or C8 -- and its palette lives
// in a sibling PLT0 resource rather than in the TEX0 itself. SaveTEX() strips
// the palette, which is right for a lone .tex0 on disk but converts an
// archived texture to a format the game does not expect. This checks that the
// paletted writer keeps the pair readable: the TEX0 stays C4, the PLT0 carries
// the colours, and reading the two back reproduces the picture.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "types.h"
#include "lib-image.h"
#include "lib-plt0.h"
#ifdef __cplusplus
}
#endif

extern void trace_free (const char *func, const char *file, unsigned int line, void *ptr);
extern void *trace_calloc (const char *func, const char *file, unsigned int line, size_t nmemb, size_t size);
extern void *trace_malloc (const char *func, const char *file, unsigned int line, size_t size);
#define free(p) trace_free(__FUNCTION__, __FILE__, __LINE__, (p))
#define calloc(n, s) trace_calloc(__FUNCTION__, __FILE__, __LINE__, (n), (s))
#define malloc(s) trace_malloc(__FUNCTION__, __FILE__, __LINE__, (s))

#define W 16
#define H 16

static int fail = 0;

static void check (int cond, const char *what)
{
	if (cond)
		printf ("  ok   %s\n", what);
	else
	{
		printf ("  FAIL %s\n", what);
		fail = 1;
	}
}

// Eight well separated colours, laid out in horizontal bands so a palette of
// 16 entries can hold every one of them exactly and the round trip is lossless.
static void fill_source (u8 *rgba, uint xwidth)
{
	static const u8 band[8][4] = {
		{ 0xff, 0x00, 0x00, 0xff }, { 0x00, 0xff, 0x00, 0xff },
		{ 0x00, 0x00, 0xff, 0xff }, { 0xff, 0xff, 0x00, 0xff },
		{ 0xff, 0x00, 0xff, 0xff }, { 0x00, 0xff, 0xff, 0xff },
		{ 0xff, 0xff, 0xff, 0xff }, { 0x00, 0x00, 0x00, 0xff },
	};
	for (uint y = 0; y < H; y++)
		for (uint x = 0; x < W; x++)
			memcpy (rgba + 4 * (y * xwidth + x), band[(y / 2) & 7], 4);
}

int main (void)
{
	const char *tex_path = "/tmp/_r_plt0_tex.tex0";
	const char *plt_path = "/tmp/_r_plt0_tex.plt0";
	remove (tex_path);
	remove (plt_path);

	Image_t img;
	InitializeIMG (&img);
	img.iform = IMG_X_RGB;
	img.width = img.xwidth = W;
	img.height = img.xheight = H;
	img.data_size = W * H * 4;
	img.data = CALLOC (1, img.data_size);
	img.data_alloced = true;
	fill_source (img.data, img.xwidth);

	u8 *want = MALLOC (img.data_size);
	memcpy (want, img.data, img.data_size);

	enumError err = ConvertIMG (&img, false, 0, IMG_C4, PAL_RGB565);
	check (!err, "convert source to C4");
	check (img.iform == IMG_C4, "converted image reports C4");
	check (img.pal && img.n_pal > 0, "converted image carries a palette");

	err = SaveTEXwithPLT0 (&img, 0, tex_path, plt_path, true);
	check (!err, "SaveTEXwithPLT0 writes both files");

	u8 *tex = 0, *plt = 0;
	uint tex_size = 0, plt_size = 0;
	{
		FILE *f = fopen (tex_path, "rb");
		check (f != 0, "TEX0 file exists");
		if (!f)
			return 1;
		fseek (f, 0, SEEK_END);
		tex_size = ftell (f);
		fseek (f, 0, SEEK_SET);
		tex = MALLOC (tex_size);
		if (fread (tex, 1, tex_size, f) != tex_size)
			fail = 1;
		fclose (f);

		f = fopen (plt_path, "rb");
		check (f != 0, "PLT0 file exists");
		if (!f)
			return 1;
		fseek (f, 0, SEEK_END);
		plt_size = ftell (f);
		fseek (f, 0, SEEK_SET);
		plt = MALLOC (plt_size);
		if (fread (plt, 1, plt_size, f) != plt_size)
			fail = 1;
		fclose (f);
	}

	check (tex_size > 0x40 && !memcmp (tex, "TEX0", 4), "TEX0 magic");
	check (plt_size > 0x40 && !memcmp (plt, "PLT0", 4), "PLT0 magic");

	palette_format_t pform = PAL_INVALID;
	uint n_pal = 0;
	const u8 *raw_pal = 0;
	check (GetRawPLT0 (plt, plt_size, &pform, &n_pal, &raw_pal), "PLT0 parses back");
	check (n_pal == img.n_pal, "PLT0 holds every palette entry");
	check (raw_pal && !memcmp (raw_pal, img.pal, n_pal * 2),
		"PLT0 palette matches the image's own, unrequantized");

	// Read the pair back the way the archive path does, through the external
	// palette hook, and confirm the picture survived.
	Image_t back;
	InitializeIMG (&back);
	err = LoadIMG (&back, true, tex_path, 0, false, false, false);
	check (!err, "reload the TEX0");
	if (!err)
	{
		// A TEX0's indexed pixels carry no palette of their own, so the
		// sibling PLT0's colours are attached before expanding, exactly as
		// the archive path does through ExportPNG()'s external-palette hook.
		// The stored format is read back out of the file, so this is what
		// proves the TEX0 kept C4 rather than being flattened on write.
		check (back.iform == IMG_C4, "TEX0 records C4, not a direct-colour format");
		if (back.pal && back.pal_alloced)
			FREE (back.pal);
		back.pal = MALLOC (n_pal * 2);
		memcpy (back.pal, raw_pal, n_pal * 2);
		back.pal_size = n_pal * 2;
		back.n_pal = n_pal;
		back.pal_alloced = true;
		back.pform = pform;
		{
			err = ConvertIMG (&back, false, 0, IMG_X_RGB, PAL_INVALID);
			check (!err, "expand back to RGB");
			check (back.width == W && back.height == H, "dimensions survive");
			if (!err && back.data)
			{
				int same = 1;
				for (uint y = 0; y < H && same; y++)
					for (uint x = 0; x < W; x++)
					{
						const u8 *a = want + 4 * (y * W + x);
						const u8 *b = back.data + 4 * (y * back.xwidth + x);
						if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2])
						{
							same = 0;
							break;
						}
					}
				check (same, "pixels round trip through TEX0+PLT0");
			}
		}
	}
	ResetIMG (&back);

	FREE (tex);
	FREE (plt);
	FREE (want);
	ResetIMG (&img);

	printf (fail ? "RESULT: FAIL\n" : "RESULT: ok\n");
	return fail;
}
