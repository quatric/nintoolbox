// Professor Layton backgrounds: BGR555 palette, 8-bit tiles and a tile map.
// Layout cross-checked with LaytonEditor's public-domain formats/graphics/bg.py.
#include "lib-layton-bg.h"
#include "lib-nintendo.h"
#include "lib-huff.h"
#include "lib-nintendo-rl.h"

typedef struct layton_bg_t
{
	uint colors, tiles, columns, rows;
	const u8 *palette, *pixels, *map;
} layton_bg_t;

static bool scan_bg (layton_bg_t *bg, const u8 *src, uint size)
{
	if (!src || size < 12)
		return false;
	bg->colors = rd_le32 (src);
	if (!bg->colors || bg->colors > 256 || size < 12 + bg->colors * 2)
		return false;
	bg->palette = src + 4;
	const uint tile_offset = 8 + bg->colors * 2;
	bg->tiles = rd_le32 (src + tile_offset - 4);
	if (!bg->tiles || bg->tiles > 65536 || (u64)tile_offset + bg->tiles * 64 + 4 > size)
		return false;
	bg->pixels = src + tile_offset;
	const uint map_offset = tile_offset + bg->tiles * 64;
	bg->columns = rd_le16 (src + map_offset);
	bg->rows = rd_le16 (src + map_offset + 2);
	if (!bg->columns || !bg->rows || bg->columns > 512 || bg->rows > 512
		|| (u64)map_offset + 4 + (u64)bg->columns * bg->rows * 2 != size)
		return false;
	bg->map = src + map_offset + 4;
	// Validate even unused tiles, before allocating or drawing any pixels.
	for (uint i = 0; i < bg->tiles * 64; i++)
		if (bg->pixels[i] >= bg->colors)
			return false;
	for (uint i = 0; i < bg->columns * bg->rows; i++)
		if (rd_le16 (bg->map + i * 2) >= bg->tiles)
			return false;
	return true;
}

enumError DecodeLaytonBG_RGBA (u8 **rgba, uint *width, uint *height, const u8 *src, uint size)
{
	if (!rgba || !width || !height)
		return ERR_INVALID_DATA;
	*rgba = 0;
	*width = *height = 0;
	layton_bg_t bg;
	u8 *decoded = 0;
	uint decoded_size = 0;
	if (!scan_bg (&bg, src, size))
	{
		if (!src || size < 8)
			return ERR_NOTHING_TO_DO;
		const uint type = rd_le32 (src);
		const u8 signatures[] = { 0, 0x30, 0x10, 0x24, 0x28 };
		if (!type || type > 4 || src[4] != signatures[type])
			return ERR_NOTHING_TO_DO;
		uint unpacked = rd_le32 (src + 4) >> 8;
		if (!unpacked && size >= 12)
			unpacked = rd_le32 (src + 8);
		// A background's bounded palette, tile set and map fit within 8 MiB.
		if (!unpacked || unpacked > (8u << 20))
			return ERR_INVALID_DATA;
		enumError err = type == 1 ? DecodeNintendoRL (&decoded, &decoded_size, src + 4, size - 4)
			: type == 2			  ? DecodeLZ10LZ11 (&decoded, &decoded_size, src + 4, size - 4)
								  : DecodeNintendoHuff (&decoded, &decoded_size, src + 4, size - 4);
		if (err)
		{
			FREE (decoded);
			return ERR_INVALID_DATA;
		}
		if (!scan_bg (&bg, decoded, decoded_size))
		{
			FREE (decoded);
			return ERR_NOTHING_TO_DO;
		}
	}
	const uint w = bg.columns * 8, h = bg.rows * 8;
	u8 *out = MALLOC ((size_t)w * h * 4);
	if (!out)
	{
		FREE (decoded);
		return ERR_OUT_OF_MEMORY;
	}
	u8 palette[256][4];
	for (uint i = 0; i < bg.colors; i++)
	{
		const uint color = rd_le16 (bg.palette + i * 2);
		for (uint c = 0; c < 3; c++)
		{
			const uint component = (color >> (c * 5)) & 31;
			palette[i][c] = (component << 3) | (component >> 2);
		}
		palette[i][3] = i || (color & 0x8000) ? 255 : 0;
	}
	for (uint y = 0; y < h; y++)
		for (uint x = 0; x < w; x++)
		{
			const uint tile = rd_le16 (bg.map + ((y / 8) * bg.columns + x / 8) * 2);
			const uint color = bg.pixels[tile * 64 + (y & 7) * 8 + (x & 7)];
			memcpy (out + ((size_t)y * w + x) * 4, palette[color], 4);
		}
	FREE (decoded);
	*rgba = out;
	*width = w;
	*height = h;
	return ERR_OK;
}
