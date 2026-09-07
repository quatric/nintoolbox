// Wii BRFNT font format -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

enumError EncodeBRFNT_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, uint cell_w, uint cell_h)
{
	if (!dest || !dest_size || !rgba || !width || !height)
		return ERR_SEMANTIC;
	*dest = 0;
	*dest_size = 0;

	if (!cell_w)
		cell_w = (width >= 16) ? 16 : width;
	if (!cell_h)
		cell_h = (height >= 16) ? 16 : height;
	uint cols = width / cell_w;
	uint rows = height / cell_h;
	if (!cols)
		cols = 1;
	if (!rows)
		rows = 1;
	uint n_chars = cols * rows;

	uint tw = (width + 3) & ~3u;
	uint th = (height + 3) & ~3u;
	uint tex_size = tw * th * 4;
	u8 *tex_data = CALLOC (1, tex_size);
	if (!tex_data)
		return ERR_OUT_OF_MEMORY;

	uint out_idx = 0;
	for (uint by = 0; by < th; by += 4)
	{
		for (uint bx = 0; bx < tw; bx += 4)
		{
			// 16 AR pairs
			for (uint y = 0; y < 4; y++)
			{
				for (uint x = 0; x < 4; x++)
				{
					uint px = bx + x;
					uint py = by + y;
					u8 r = 0, g = 0, b = 0, a = 0;
					if (px < width && py < height)
					{
						uint idx = (py * width + px) * 4;
						r = rgba[idx];
						g = rgba[idx + 1];
						b = rgba[idx + 2];
						a = rgba[idx + 3];
					}
					tex_data[out_idx++] = a;
					tex_data[out_idx++] = r;
				}
			}
			// 16 GB pairs
			for (uint y = 0; y < 4; y++)
			{
				for (uint x = 0; x < 4; x++)
				{
					uint px = bx + x;
					uint py = by + y;
					u8 r = 0, g = 0, b = 0, a = 0;
					if (px < width && py < height)
					{
						uint idx = (py * width + px) * 4;
						r = rgba[idx];
						g = rgba[idx + 1];
						b = rgba[idx + 2];
						a = rgba[idx + 3];
					}
					tex_data[out_idx++] = g;
					tex_data[out_idx++] = b;
				}
			}
		}
	}

	uint tglp_size = (0x30 + tex_size + 3) & ~3u;
	uint cwdh_payload_size = n_chars * 3;
	uint cwdh_size = (0x10 + cwdh_payload_size + 3) & ~3u;
	uint cmap_size = 0x14;
	uint total_size = 0x10 + 0x20 + tglp_size + cwdh_size + cmap_size;

	u8 *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (tex_data);
		return ERR_OUT_OF_MEMORY;
	}

	// Header (16 bytes)
	memcpy (out, "RFNT", 4);
	wr_be16 (out + 4, 0xFEFF); // BOM
	wr_be16 (out + 6, 0x0104); // version
	wr_be32 (out + 8, total_size);
	wr_be16 (out + 12, 0x0010); // header size
	wr_be16 (out + 14, 0x0004); // sections count

	// FINF (32 bytes) at offset 0x10
	u8 *finf = out + 0x10;
	memcpy (finf, "FINF", 4);
	wr_be32 (finf + 4, 0x00000020);
	finf[8] = 0; // glyph
	finf[9] = (u8)cell_h; // line feed
	wr_be16 (finf + 10, 0); // alter char
	finf[12] = 0; // left space
	finf[13] = (u8)cell_w; // glyph width
	finf[14] = (u8)cell_w; // char width
	finf[15] = 0; // UTF-8 encoding

	uint tglp_off = 0x30;
	uint cwdh_off = tglp_off + tglp_size;
	uint cmap_off = cwdh_off + cwdh_size;

	wr_be32 (finf + 16, tglp_off + 8); // ptr to TGLP data
	wr_be32 (finf + 20, cwdh_off + 8); // ptr to CWDH data
	wr_be32 (finf + 24, cmap_off + 8); // ptr to CMAP data
	finf[28] = (u8)cell_h; // height
	finf[29] = (u8)cell_w; // width
	finf[30] = (u8)(cell_h > 2 ? cell_h - 2 : cell_h); // ascent
	finf[31] = 0; // reserved

	// TGLP at offset 0x30
	u8 *tglp = out + tglp_off;
	memcpy (tglp, "TGLP", 4);
	wr_be32 (tglp + 4, tglp_size);
	tglp[8] = (u8)cell_w;
	tglp[9] = (u8)cell_h;
	tglp[10] = (u8)(cell_h > 2 ? cell_h - 2 : cell_h); // baseline
	tglp[11] = (u8)cell_w; // max char width
	wr_be32 (tglp + 12, tex_size); // sheet size
	wr_be16 (tglp + 16, 1); // sheet count
	wr_be16 (tglp + 18, 6); // format = RGBA8
	wr_be16 (tglp + 20, (u16)rows);
	wr_be16 (tglp + 22, (u16)cols);
	wr_be16 (tglp + 24, (u16)width);
	wr_be16 (tglp + 26, (u16)height);
	wr_be32 (tglp + 28, tglp_off + 0x30); // data offset
	memcpy (tglp + 0x30, tex_data, tex_size);
	FREE (tex_data);

	// CWDH at cwdh_off
	u8 *cwdh = out + cwdh_off;
	memcpy (cwdh, "CWDH", 4);
	wr_be32 (cwdh + 4, cwdh_size);
	wr_be16 (cwdh + 8, 0); // first index
	wr_be16 (cwdh + 10, (u16)(n_chars - 1)); // last index
	wr_be32 (cwdh + 12, 0); // next cwdh
	for (uint i = 0; i < n_chars; i++)
	{
		cwdh[16 + i * 3] = 0;
		cwdh[16 + i * 3 + 1] = (u8)cell_w;
		cwdh[16 + i * 3 + 2] = (u8)cell_w;
	}

	// CMAP at cmap_off
	u8 *cmap = out + cmap_off;
	memcpy (cmap, "CMAP", 4);
	wr_be32 (cmap + 4, cmap_size);
	wr_be16 (cmap + 8, 0x0020); // first char code
	wr_be16 (cmap + 10, (u16)(0x0020 + n_chars - 1)); // last char code
	wr_be16 (cmap + 12, 0); // direct mapping
	wr_be16 (cmap + 14, 0); // reserved
	wr_be32 (cmap + 16, 0); // next cmap
	wr_be32 (cmap + 20, 0); // index offset

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}
