// Luminous Arc 2 SCB maps with IMB tiles and PLB BGR555 palettes.
#include "lib-luminous-bg.h"
#include "lib-nintendo.h"

enumError DecodeLuminousBG_RGBA (u8 **rgba, uint *width, uint *height, const u8 *map, uint map_size,
	const u8 *tiles, uint tile_size, const u8 *palette, uint palette_size)
{
	if (!rgba || !width || !height)
		return ERR_INVALID_DATA;
	*rgba = 0;
	*width = *height = 0;
	if (!map || map_size < 16 || !tiles || !palette || (palette_size != 32 && palette_size != 512))
		return ERR_INVALID_DATA;
	const uint columns = rd_le32 (map), rows = rd_le32 (map + 4);
	const uint bytes_per_tile = palette_size == 32 ? 32 : 64;
	if (!columns || !rows || columns > 512 || rows > 512 || rd_le32 (map + 8) != 16
		|| rd_le32 (map + 12) != columns * 2 || (u64)16 + (u64)columns * rows * 2 != map_size
		|| !tile_size || tile_size % bytes_per_tile || tile_size / bytes_per_tile > 1024)
		return ERR_INVALID_DATA;
	for (uint i = 0; i < columns * rows; i++)
	{
		const uint entry = rd_le16 (map + 16 + i * 2);
		// Only single-palette resources have been verified.
		if ((entry >> 12) || (entry & 1023) >= tile_size / bytes_per_tile)
			return ERR_INVALID_DATA;
	}
	const uint w = columns * 8, h = rows * 8;
	u8 *out = MALLOC ((size_t)w * h * 4);
	if (!out)
		return ERR_OUT_OF_MEMORY;
	for (uint y = 0; y < h; y++)
		for (uint x = 0; x < w; x++)
		{
			const uint entry = rd_le16 (map + 16 + ((y / 8) * columns + x / 8) * 2);
			const uint tx = (x & 7) ^ (entry & 0x400 ? 7 : 0);
			const uint ty = (y & 7) ^ (entry & 0x800 ? 7 : 0);
			const uint pixel = ty * 8 + tx;
			const u8 *tile = tiles + (entry & 1023) * bytes_per_tile;
			const uint index
				= bytes_per_tile == 64 ? tile[pixel] : (tile[pixel / 2] >> ((pixel & 1) * 4)) & 15;
			const uint color = rd_le16 (palette + index * 2);
			u8 *dest = out + ((size_t)y * w + x) * 4;
			for (uint c = 0; c < 3; c++)
			{
				const uint value = (color >> (5 * c)) & 31;
				dest[c] = (value << 3) | (value >> 2);
			}
			dest[3] = index ? 255 : 0;
		}
	*rgba = out;
	*width = w;
	*height = h;
	return ERR_OK;
}

static enumError unpack_bg (u8 **owned, const u8 **data, uint *size)
{
	if (*size >= 2 && !memcmp (*data, "Le", 2))
	{
		if (*size < 6 || rd_le32 (*data + 2) > (8u << 20))
			return ERR_INVALID_DATA;
		uint length = 0;
		enumError err = DecodeLZE (owned, &length, *data, *size);
		if (err)
			return err;
		*data = *owned;
		*size = length;
	}
	return ERR_OK;
}

enumError LoadLuminousBG_RGBA (
	u8 **rgba, uint *width, uint *height, const u8 *map, uint map_size, ccp filename)
{
	if (!rgba || !width || !height)
		return ERR_INVALID_DATA;
	*rgba = 0;
	*width = *height = 0;
	ccp ext = filename ? strrchr (filename, '.') : 0;
	if (!ext || strlen (filename) >= PATH_MAX - 1)
		return ERR_INVALID_DATA;
	char path[PATH_MAX];
	u8 *tiles = 0, *palette = 0, *map_decoded = 0, *tiles_decoded = 0;
	size_t tile_size = 0, palette_size = 0;
	snprintf (path, sizeof (path), "%.*s.imb", (int)(ext - filename), filename);
	enumError err = LoadFileAlloc (path, 0, 0, &tiles, &tile_size, 8u << 20, 2, 0, false);
	if (!err)
	{
		snprintf (path, sizeof (path), "%.*s.plb", (int)(ext - filename), filename);
		err = LoadFileAlloc (path, 0, 0, &palette, &palette_size, 512, 2, 0, false);
	}
	const u8 *tile_data = tiles;
	uint tile_length = tile_size;
	if (!err)
		err = unpack_bg (&map_decoded, &map, &map_size);
	if (!err)
		err = unpack_bg (&tiles_decoded, &tile_data, &tile_length);
	if (!err)
		err = DecodeLuminousBG_RGBA (
			rgba, width, height, map, map_size, tile_data, tile_length, palette, palette_size);
	FREE (tiles);
	FREE (palette);
	FREE (map_decoded);
	FREE (tiles_decoded);
	return err;
}
