// Nintendo DS NCLR palette format -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

enumError DecodeNCLR_RGBA (u8 **dest, uint *width, uint *height, const u8 *src, uint src_size)
{
	if (!dest || !width || !height || !src || src_size < 0x28 || memcmp (src, "RLCN", 4)
		|| memcmp (src + 0x10, "TTLP", 4))
		return EINVAL;

	// TTLP's data offset is relative to TTLP+8.  It is normally 0x10,
	// yielding palette data at file offset 0x28.
	const uint depth = rd_le32 (src + 0x18);
	const uint data_size = rd_le32 (src + 0x20);
	const uint data_off = 0x18 + rd_le32 (src + 0x24);
	if ((depth != 3 && depth != 4) || !data_size || data_size & 1 || data_off > src_size
		|| data_size > src_size - data_off)
		return EINVAL;
	const uint entries = data_size / 2;
	const uint max_entries = 1024;
	if (!entries || entries > max_entries)
		return EINVAL;

	const uint cell = 8, cols = 16, rows = (entries + cols - 1) / cols;
	const uint w = cols * cell, h = rows * cell;
	if ((u64)w * h > NFMT_MAX_OUTPUT / 4)
		return EFBIG;
	u8 *out = MALLOC (w * h * 4);
	if (!out)
		return ERR_CANT_CREATE;

	for (uint entry = 0; entry < entries; entry++)
	{
		const u16 c = rd_le16 (src + data_off + 2 * entry);
		const u8 r = (c & 31) * 255 / 31;
		const u8 g = ((c >> 5) & 31) * 255 / 31;
		const u8 b = ((c >> 10) & 31) * 255 / 31;
		for (uint y = 0; y < cell; y++)
			for (uint x = 0; x < cell; x++)
			{
				u8 *p = out + 4 * ((entry / cols * cell + y) * w + entry % cols * cell + x);
				p[0] = r;
				p[1] = g;
				p[2] = b;
				p[3] = 255;
			}
	}
	*dest = out;
	*width = w;
	*height = h;
	return ERR_OK;
}

enumError EncodeNCLR_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height)
{
	if (!dest || !dest_size || !rgba || !width || !height)
		return EINVAL;

	uint n_colors = 0;
	u16 colors[256] = { 0 };

	// If formatted as 8x8 swatch tiles (16 cols x N rows * 8)
	if (width >= 8 && height >= 8 && (width % 8 == 0) && (height % 8 == 0))
	{
		const uint cols = width / 8;
		const uint rows = height / 8;
		const uint total_swatches = cols * rows;
		n_colors = total_swatches > 256 ? 256 : total_swatches;
		for (uint i = 0; i < n_colors; i++)
		{
			const uint sx = (i % cols) * 8 + 4;
			const uint sy = (i / cols) * 8 + 4;
			const u8 *p = rgba + 4 * (sy * width + sx);
			const u16 r = (p[0] * 31 + 127) / 255;
			const u16 g = (p[1] * 31 + 127) / 255;
			const u16 b = (p[2] * 31 + 127) / 255;
			colors[i] = (r & 31) | ((g & 31) << 5) | ((b & 31) << 10);
		}
	}
	else
	{
		const uint total_pixels = width * height;
		n_colors = total_pixels > 256 ? 256 : total_pixels;
		for (uint i = 0; i < n_colors; i++)
		{
			const u8 *p = rgba + 4 * i;
			const u16 r = (p[0] * 31 + 127) / 255;
			const u16 g = (p[1] * 31 + 127) / 255;
			const u16 b = (p[2] * 31 + 127) / 255;
			colors[i] = (r & 31) | ((g & 31) << 5) | ((b & 31) << 10);
		}
	}

	if (n_colors == 0)
		n_colors = 16;
	const uint total_colors = n_colors <= 16 ? 16 : 256;
	const uint data_size = total_colors * 2;
	const uint total_size = 0x28 + data_size;

	u8 *out = CALLOC (1, total_size);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, "RLCN", 4);
	wr_le16 (out + 4, 0xFFFE);
	wr_le16 (out + 6, 0x0100);
	wr_le32 (out + 8, total_size);
	wr_le16 (out + 12, 16);
	wr_le16 (out + 14, 1);

	u8 *ttlp = out + 16;
	memcpy (ttlp, "TTLP", 4);
	wr_le32 (ttlp + 4, 0x18 + data_size);
	wr_le32 (ttlp + 8, total_colors <= 16 ? 3 : 4);
	wr_le32 (ttlp + 12, 0);
	wr_le32 (ttlp + 16, data_size);
	wr_le32 (ttlp + 20, 0x10);

	for (uint i = 0; i < total_colors; i++)
		wr_le16 (ttlp + 0x18 + 2 * i, colors[i]);

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}
