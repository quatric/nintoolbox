#include "lib-std.h"
#include "lib-nut.h"
#include <string.h>

static uint16_t rd_be16 (const uint8_t *p) { return (uint16_t)p[0] << 8 | p[1]; }
static uint32_t rd_be32 (const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t rd_le16 (const uint8_t *p) { return (uint16_t)p[1] << 8 | p[0]; }
static uint32_t rd_le32 (const uint8_t *p) { return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]; }

bool IsNUT (const uint8_t *data, size_t size)
{
	if (!data || size < 16)
		return false;
	return !memcmp (data, "NTP3", 4) || !memcmp (data, "NTWU", 4);
}

int ParseNUT (nut_t *nut, const uint8_t *data, size_t size)
{
	if (!IsNUT (data, size) || !nut)
		return 0;
	memset (nut, 0, sizeof (*nut));

	const bool is_be = !memcmp (data, "NTWU", 4);
	nut->version = is_be ? rd_be16 (data + 4) : rd_le16 (data + 4);
	nut->count = is_be ? rd_be16 (data + 6) : rd_le16 (data + 6);

	if (!nut->count || nut->count > 0x4000)
		return 0;

	nut->textures = CALLOC (nut->count, sizeof (nut_texture_t));
	if (!nut->textures)
		return 0;

	size_t pos = 16;
	for (uint16_t i = 0; i < nut->count; i++)
	{
		if (pos + 0x50 > size)
			break;
		nut_texture_t *t = &nut->textures[i];
		t->total_size = is_be ? rd_be32 (data + pos) : rd_le32 (data + pos);
		t->data_size = is_be ? rd_be32 (data + pos + 8) : rd_le32 (data + pos + 8);
		t->header_size = is_be ? rd_be16 (data + pos + 12) : rd_le16 (data + pos + 12);
		t->width = is_be ? rd_be16 (data + pos + 24) : rd_le16 (data + pos + 24);
		t->height = is_be ? rd_be16 (data + pos + 26) : rd_le16 (data + pos + 26);
		t->pixel_format = is_be ? rd_be32 (data + pos + 32) : rd_le32 (data + pos + 32);
		t->mipmaps = is_be ? rd_be16 (data + pos + 36) : rd_le16 (data + pos + 36);
		t->hash = is_be ? rd_be32 (data + pos + 48) : rd_le32 (data + pos + 48);

		size_t data_off = pos + (t->header_size ? t->header_size : 0x50);
		if (data_off < size)
		{
			t->offset = (uint32_t)data_off;
			t->data = data + data_off;
		}

		pos += t->total_size ? t->total_size : (0x50 + t->data_size);
	}
	return 1;
}

void FreeNUT (nut_t *nut)
{
	if (!nut)
		return;
	FREE (nut->textures);
	memset (nut, 0, sizeof (*nut));
}

// Decode BC1/DXT1 block
static void decode_bc1_block (const uint8_t *src, uint8_t *dst, uint32_t stride)
{
	uint16_t c0 = (uint16_t)src[0] | ((uint16_t)src[1] << 8);
	uint16_t c1 = (uint16_t)src[2] | ((uint16_t)src[3] << 8);
	uint8_t rgb[4][4];

	rgb[0][0] = (uint8_t)(((c0 >> 11) & 0x1F) * 255 / 31);
	rgb[0][1] = (uint8_t)(((c0 >> 5) & 0x3F) * 255 / 63);
	rgb[0][2] = (uint8_t)((c0 & 0x1F) * 255 / 31);
	rgb[0][3] = 255;

	rgb[1][0] = (uint8_t)(((c1 >> 11) & 0x1F) * 255 / 31);
	rgb[1][1] = (uint8_t)(((c1 >> 5) & 0x3F) * 255 / 63);
	rgb[1][2] = (uint8_t)((c1 & 0x1F) * 255 / 31);
	rgb[1][3] = 255;

	if (c0 > c1)
	{
		rgb[2][0] = (uint8_t)((2 * rgb[0][0] + rgb[1][0]) / 3);
		rgb[2][1] = (uint8_t)((2 * rgb[0][1] + rgb[1][1]) / 3);
		rgb[2][2] = (uint8_t)((2 * rgb[0][2] + rgb[1][2]) / 3);
		rgb[2][3] = 255;

		rgb[3][0] = (uint8_t)((rgb[0][0] + 2 * rgb[1][0]) / 3);
		rgb[3][1] = (uint8_t)((rgb[0][1] + 2 * rgb[1][1]) / 3);
		rgb[3][2] = (uint8_t)((rgb[0][2] + 2 * rgb[1][2]) / 3);
		rgb[3][3] = 255;
	}
	else
	{
		rgb[2][0] = (uint8_t)((rgb[0][0] + rgb[1][0]) / 2);
		rgb[2][1] = (uint8_t)((rgb[0][1] + rgb[1][1]) / 2);
		rgb[2][2] = (uint8_t)((rgb[0][2] + rgb[1][2]) / 2);
		rgb[2][3] = 255;

		rgb[3][0] = rgb[3][1] = rgb[3][2] = 0;
		rgb[3][3] = 0;
	}

	for (int y = 0; y < 4; y++)
	{
		uint8_t bits = src[4 + y];
		for (int x = 0; x < 4; x++)
		{
			uint8_t idx = (bits >> (x * 2)) & 3;
			memcpy (dst + y * stride + x * 4, rgb[idx], 4);
		}
	}
}

int DecodeNUTTextureToRGBA (const nut_texture_t *tex, uint8_t **out_rgba, uint32_t *out_w, uint32_t *out_h)
{
	if (!tex || !tex->data || !tex->width || !tex->height || !out_rgba)
		return 0;

	uint32_t w = tex->width;
	uint32_t h = tex->height;
	uint8_t *rgba = MALLOC ((size_t)w * h * 4);
	if (!rgba)
		return 0;

	if (tex->pixel_format == 0) // DXT1 / BC1
	{
		uint32_t bw = (w + 3) / 4;
		uint32_t bh = (h + 3) / 4;
		const uint8_t *src = tex->data;
		for (uint32_t by = 0; by < bh; by++)
		{
			for (uint32_t bx = 0; bx < bw; bx++)
			{
				uint8_t blk[4 * 4 * 4];
				decode_bc1_block (src, blk, 16);
				src += 8;
				for (uint32_t py = 0; py < 4 && by * 4 + py < h; py++)
				{
					for (uint32_t px = 0; px < 4 && bx * 4 + px < w; px++)
					{
						size_t dp = ((by * 4 + py) * w + (bx * 4 + px)) * 4;
						size_t sp = (py * 4 + px) * 4;
						memcpy (rgba + dp, blk + sp, 4);
					}
				}
			}
		}
	}
	else if (tex->pixel_format == 8 || tex->pixel_format == 14) // RGBA8
	{
		memcpy (rgba, tex->data, (size_t)w * h * 4);
	}
	else
	{
		// Fallback: clear to white
		memset (rgba, 255, (size_t)w * h * 4);
	}

	*out_rgba = rgba;
	if (out_w) *out_w = w;
	if (out_h) *out_h = h;
	return 1;
}
