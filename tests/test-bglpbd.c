// SPDX-License-Identifier: GPL-2.0+
#include "lib-nintendo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "lib-bglpbd.h"
#include "lib-aamp.h"

extern void trace_free (const char *func, const char *file, unsigned int line, void *ptr);
extern void *trace_malloc (const char *func, const char *file, unsigned int line, size_t size);
extern void *trace_calloc (
	const char *func, const char *file, unsigned int line, size_t nmemb, size_t size);
extern void *trace_realloc (
	const char *func, const char *file, unsigned int line, void *ptr, size_t size);
extern char *trace_strdup (const char *func, const char *file, unsigned int line, const char *str);
#define free(p) trace_free (__FUNCTION__, __FILE__, __LINE__, (p))
#define malloc(s) trace_malloc (__FUNCTION__, __FILE__, __LINE__, (s))
#define calloc(n, s) trace_calloc (__FUNCTION__, __FILE__, __LINE__, (n), (s))
#define realloc(p, s) trace_realloc (__FUNCTION__, __FILE__, __LINE__, (p), (s))
#define strdup(s) trace_strdup (__FUNCTION__, __FILE__, __LINE__, (s))

static void test_sh_math (void)
{
	float sh[27];
	float color[3] = { 1.0f, 0.5f, 0.25f };
	SH_SetConstantColor (sh, color);
	assert (fabsf (sh[0] - 1.0f) < 1e-6f);
	assert (fabsf (sh[9] - 0.5f) < 1e-6f);
	assert (fabsf (sh[18] - 0.25f) < 1e-6f);

	float rgb_sh[7][4];
	SH_ConvertSH2RGB (sh, rgb_sh);
	float normal[3] = { 0.0f, 1.0f, 0.0f };
	float rgb_out[3];
	SH_GetRGBColor (normal, rgb_sh, rgb_out);
	assert (rgb_out[0] >= 0.0f && rgb_out[1] >= 0.0f && rgb_out[2] >= 0.0f);
	printf ("SH Math: OK\n");
}

static void test_unity_import_and_roundtrip (void)
{
	const char *unity_txt =
		"8;( -50.0, -25.0, -50.0 );( 50.0, 25.0, 50.0 );50.0\n"
		"0.8\n0.1\n0.2\n0.0\n0.0\n0.0\n0.0\n0.0\n0.0\n"
		"0.8\n0.1\n0.2\n0.0\n0.0\n0.0\n0.0\n0.0\n0.0\n"
		"0.8\n0.1\n0.2\n0.0\n0.0\n0.0\n0.0\n0.0\n0.0\n";

	bglpbd_file_t file;
	enumError err = CreateBGLPBD_FromUnityText (&file, unity_txt, strlen (unity_txt), NULL, true);
	assert (err == ERR_OK);
	assert (file.num_boxes == 1);
	assert (file.boxes[0].num_indices > 0);

	u8 *bin = NULL;
	size_t bin_sz = 0;
	err = EncodeBGLPBD (&file, &bin, &bin_sz);
	assert (err == ERR_OK);
	assert (bin != NULL && bin_sz > 0);
	assert (IsBGLPBD (bin, bin_sz));

	bglpbd_file_t loaded;
	err = ScanBGLPBD (&loaded, bin, bin_sz);
	assert (err == ERR_OK);
	assert (loaded.num_boxes == 1);
	assert (loaded.boxes[0].num_indices == file.boxes[0].num_indices);
	assert (loaded.boxes[0].num_sh_data == file.boxes[0].num_sh_data);

	ResetBGLPBD (&loaded);
	ResetBGLPBD (&file);
	free (bin);
	printf ("Unity Import & Roundtrip: OK\n");
}

static void test_bounds_generation (void)
{
	float min_p[3] = { -500.0f, -200.0f, -500.0f };
	float max_p[3] = { 500.0f, 200.0f, 500.0f };
	float step[3] = { 500.0f, 500.0f, 500.0f };

	bglpbd_file_t file;
	enumError err = CreateBGLPBD_FromBounds (&file, min_p, max_p, step, NULL, false); // Wii U
	assert (err == ERR_OK);
	assert (!file.is_switch);
	assert (file.num_boxes == 1);
	assert (file.boxes[0].num_sh_data == 8);

	u8 *bin = NULL;
	size_t bin_sz = 0;
	err = EncodeBGLPBD (&file, &bin, &bin_sz);
	assert (err == ERR_OK);
	assert (IsBGLPBD (bin, bin_sz));

	bglpbd_file_t loaded;
	err = ScanBGLPBD (&loaded, bin, bin_sz);
	assert (err == ERR_OK);
	assert (!loaded.is_switch);
	assert (loaded.num_boxes == 1);

	ResetBGLPBD (&loaded);
	ResetBGLPBD (&file);
	free (bin);
	printf ("Bounds Generation (Wii U): OK\n");
}

int main (void)
{
	AAMP_InitHashDB ();
	test_sh_math ();
	test_unity_import_and_roundtrip ();
	test_bounds_generation ();
	printf ("All BGLPBD tests passed successfully!\n");
	return 0;
}
