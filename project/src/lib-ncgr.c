// Nintendo DS NCGR tiled-graphics format -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

enumError DecodeNCGR_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size)
{
	if (!dest || !width || !height || !src || src_size < 0x30 || memcmp (src, "RGCN", 4)
		|| memcmp (src + 0x10, "RAHC", 4))
		return EINVAL;
	// Nitro's RAHC header stores the data byte count and an offset relative
	// to RAHC+8.  The normal resource layout has data at RAHC+0x20.
	const u8 *rahc = src + 0x10;
	const uint num_y = rd_le16 (rahc + 0x08);
	const uint num_x = rd_le16 (rahc + 0x0a);
	const uint depth = rd_le32 (rahc + 0x0c);
	const uint data_size = rd_le32 (rahc + 0x18);
	const uint data_off = 8 + rd_le32 (rahc + 0x1c);
	const uint bpt = depth == 3 ? 32 : depth == 4 ? 64 : 0;
	if (!bpt || !data_size || data_size % bpt || data_off > src_size - 0x10
		|| data_size > src_size - (0x10 + data_off))
		return EINVAL;
	const uint n_tiles = data_size / bpt;
	uint cols = 16;
	if (num_x > 0 && num_x != 0xFFFF)
		cols = num_x;
	else if (n_tiles < 16)
		cols = n_tiles;
	else if (n_tiles % 32 == 0 && n_tiles >= 32)
		cols = 32;

	const uint rows = (num_y > 0 && num_y != 0xFFFF && num_x * num_y >= n_tiles)
		? num_y
		: (n_tiles + cols - 1) / cols;
	const uint w = 8 * cols, h = 8 * rows;
	if (!w || !h || (u64)w * h > NFMT_MAX_OUTPUT / 4)
		return EFBIG;
	u8 *out = CALLOC (1, w * h * 4);
	if (!out)
		return ERR_CANT_CREATE;
	const u8 *tiles = rahc + data_off;
	for (uint tile = 0; tile < n_tiles; tile++)
	{
		const uint tile_x = tile % cols;
		const uint tile_y = tile / cols;
		for (uint y = 0; y < 8; y++)
			for (uint x = 0; x < 8; x++)
			{
				const uint pos = tile * bpt + (depth == 3 ? 4 * y + x / 2 : 8 * y + x);
				const u8 index = depth == 3 ? (tiles[pos] >> (4 * (x & 1))) & 15 : tiles[pos];
				u8 *p = out + 4 * ((tile_y * 8 + y) * w + tile_x * 8 + x);
				p[0] = p[1] = p[2] = depth == 3 ? index * 17 : index;
				p[3] = index ? 255 : 0;
			}
	}
	*dest = out;
	*width = w;
	*height = h;
	return ERR_OK;
}

enumError EncodeNCGR_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, bool is_8bpp)
{
	if (!dest || !dest_size || !rgba || !width || !height)
		return EINVAL;

	const uint cols = (width + 7) / 8;
	const uint rows = (height + 7) / 8;
	const uint n_tiles = cols * rows;
	if (!n_tiles || n_tiles > 65535)
		return EFBIG;

	const uint bpt = is_8bpp ? 64 : 32;
	const uint tile_data_size = n_tiles * bpt;
	const uint total_size = 0x30 + tile_data_size;

	u8 *out = CALLOC (1, total_size);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, "RGCN", 4);
	wr_le16 (out + 4, 0xFFFE);
	wr_le16 (out + 6, 0x0101);
	wr_le32 (out + 8, total_size);
	wr_le16 (out + 12, 16);
	wr_le16 (out + 14, 1);

	u8 *rahc = out + 16;
	memcpy (rahc, "RAHC", 4);
	wr_le32 (rahc + 4, 0x20 + tile_data_size);
	wr_le16 (rahc + 8, (u16)rows);
	wr_le16 (rahc + 10, (u16)cols);
	wr_le32 (rahc + 12, is_8bpp ? 4 : 3);
	wr_le32 (rahc + 16, 0);
	wr_le32 (rahc + 20, 0);
	wr_le32 (rahc + 24, tile_data_size);
	wr_le32 (rahc + 28, 0x18);

	u8 *tiles = rahc + 0x20;
	for (uint tile = 0; tile < n_tiles; tile++)
	{
		const uint tile_col = tile % (cols < 16 ? cols : 16);
		const uint tile_row = tile / (cols < 16 ? cols : 16);
		for (uint y = 0; y < 8; y++)
		{
			for (uint x = 0; x < 8; x++)
			{
				const uint px = tile_col * 8 + x;
				const uint py = tile_row * 8 + y;
				u8 val = 0;
				if (px < width && py < height)
				{
					const u8 *p = rgba + 4 * (py * width + px);
					if (p[3] > 0)
					{
						if (is_8bpp)
							val = p[0];
						else
							val = (u8)((p[0] * 15 + 127) / 255);
					}
				}

				if (is_8bpp)
				{
					tiles[tile * 64 + 8 * y + x] = val;
				}
				else
				{
					const uint pos = tile * 32 + 4 * y + x / 2;
					if (x & 1)
						tiles[pos] |= (val & 15) << 4;
					else
						tiles[pos] = (val & 15);
				}
			}
		}
	}

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}
