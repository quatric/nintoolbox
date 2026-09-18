#include "lib-bntx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

void trace_free (ccp f, ccp p, uint l, void *v) { (void)f; (void)p; (void)l; free (v); }
void *trace_malloc (ccp f, ccp p, uint l, size_t n) { (void)f; (void)p; (void)l; return malloc (n); }
void *trace_calloc (ccp f, ccp p, uint l, size_t n, size_t s) { (void)f; (void)p; (void)l; return calloc (n, s); }
void *trace_realloc (ccp f, ccp p, uint l, void *v, size_t n) { (void)f; (void)p; (void)l; return realloc (v, n); }
void dclib_free (void *v) { free (v); }
void *dclib_malloc (size_t n) { return malloc (n); }
void *dclib_calloc (size_t n, size_t s) { return calloc (n, s); }
void *dclib_realloc (void *v, size_t n) { return realloc (v, n); }
enumError PrintError (enumError err, ccp format, ...) { (void)format; return err; }

int main (void)
{
	// Test GetBNTXFormatName
	assert (!strcmp (GetBNTXFormatName (0x0201), "R8_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x0301), "R4G4B4A4_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x0501), "R5G5B5A1_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x0601), "A1B5G5R5_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x0701), "R5G6B5_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x0801), "B5G6R5_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x0901), "R8G8_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x0a01), "R16_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x0a05), "R16_FLOAT"));
	assert (!strcmp (GetBNTXFormatName (0x0b01), "R8G8B8A8_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x0b06), "R8G8B8A8_SRGB"));
	assert (!strcmp (GetBNTXFormatName (0x0c01), "B8G8R8A8_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x0d05), "R9G9B9E5F_FLOAT"));
	assert (!strcmp (GetBNTXFormatName (0x0e01), "R10G10B10A2_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x0f05), "R11G11B10F_FLOAT"));
	assert (!strcmp (GetBNTXFormatName (0x1205), "R16G16_FLOAT"));
	assert (!strcmp (GetBNTXFormatName (0x1307), "D24S8_DEPTH"));
	assert (!strcmp (GetBNTXFormatName (0x1405), "R32_FLOAT"));
	assert (!strcmp (GetBNTXFormatName (0x1505), "R16G16B16A16_FLOAT"));
	assert (!strcmp (GetBNTXFormatName (0x1607), "D32FS8_DEPTH"));
	assert (!strcmp (GetBNTXFormatName (0x1705), "R32G32_FLOAT"));
	assert (!strcmp (GetBNTXFormatName (0x1805), "R32G32B32_FLOAT"));
	assert (!strcmp (GetBNTXFormatName (0x1905), "R32G32B32A32_FLOAT"));
	assert (!strcmp (GetBNTXFormatName (0x1a01), "BC1_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x1f0a), "BC6H_UF16"));
	assert (!strcmp (GetBNTXFormatName (0x2001), "BC7_UNORM"));
	// Mobile-compression family from LegacySwitchLibraries GFX/Enums.cs
	// (SurfaceFormat): named for inspection, pixel decode stays unsupported.
	assert (!strcmp (GetBNTXFormatName (0x2101), "EAC_R11_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x2201), "EAC_R11_G11_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x2301), "ETC1_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x2306), "ETC1_SRGB"));
	assert (!strcmp (GetBNTXFormatName (0x2401), "ETC2_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x2406), "ETC2_SRGB"));
	assert (!strcmp (GetBNTXFormatName (0x2501), "ETC2_MASK_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x2606), "ETC2_ALPHA_SRGB"));
	assert (!strcmp (GetBNTXFormatName (0x2701), "PVRTC1_28PP_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x2801), "PVRTC1_48PP_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x2901), "PVRTC1_ALPHA_28PP_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x2a01), "PVRTC1_ALPHA_48PP_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x2b01), "PVRTC2_ALPHA_28PP_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x2c01), "PVRTC2_ALPHA_48PP_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x2d01), "ASTC_4x4_UNORM"));
	assert (!strcmp (GetBNTXFormatName (0x3b01), "B5G5R5A1_UNORM"));

	// Test synthetic container parsing and decoding
	u8 rgba_in[16 * 16 * 4];
	for (int i = 0; i < 16 * 16; i++)
	{
		rgba_in[i * 4 + 0] = (u8)(i & 0xff);
		rgba_in[i * 4 + 1] = (u8)((i * 3) & 0xff);
		rgba_in[i * 4 + 2] = (u8)((i * 7) & 0xff);
		rgba_in[i * 4 + 3] = 255;
	}

	u8 *bntx_buf = NULL;
	uint bntx_size = 0;
	enumError err = EncodeBNTX_RGBA (&bntx_buf, &bntx_size, rgba_in, 16, 16, "test_tex");
	assert (err == ERR_OK && bntx_buf && bntx_size > 0);

	bntx_t bntx;
	err = ScanBNTX (&bntx, bntx_buf, bntx_size);
	assert (err == ERR_OK);
	assert (bntx.n_textures == 1);
	assert (!strcmp (bntx.platform, "NX  "));
	assert (!strcmp (bntx.textures[0].name, "test_tex"));
	assert (bntx.textures[0].width == 16);
	assert (bntx.textures[0].height == 16);
	assert (bntx.reloc_table.n_sections == 2);
	assert (bntx.reloc_table.n_entries == 10);

	u8 *decoded = NULL;
	uint dw = 0, dh = 0;
	err = DecodeBNTX_RGBA (&decoded, &dw, &dh, &bntx, 0);
	assert (err == ERR_OK && decoded && dw == 16 && dh == 16);
	assert (!memcmp (rgba_in, decoded, 16 * 16 * 4));

	free (decoded);
	ResetBNTX (&bntx);
	free (bntx_buf);

	printf ("All BNTX format and container tests passed successfully!\n");
	return 0;
}
