// 3DS BCFNT font format -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

enumError EncodeBCFNT_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height,
	uint cell_w, uint cell_h, bool is_wiiu)
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

	uint tw = (width + 7) & ~7u;
	uint th = (height + 7) & ~7u;
	uint tex_size = tw * th * 4;
	u8 *tex_data = CALLOC (1, tex_size);
	if (!tex_data)
		return ERR_OUT_OF_MEMORY;

	for (uint y = 0; y < height; y++)
		memcpy (tex_data + y * width * 4, rgba + y * width * 4, width * 4);

	uint tglp_size = (0x20 + tex_size + 3) & ~3u;
	uint cwdh_payload_size = n_chars * 3;
	uint cwdh_size = (0x10 + cwdh_payload_size + 3) & ~3u;
	uint cmap_size = 0x14;
	uint header_size = 0x14;
	uint total_size = header_size + 0x20 + tglp_size + cwdh_size + cmap_size;

	u8 *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (tex_data);
		return ERR_OUT_OF_MEMORY;
	}

#define CF_W16(p, v)                                                                               \
	do                                                                                             \
	{                                                                                              \
		if (is_wiiu)                                                                               \
			wr_be16 (p, v);                                                                        \
		else                                                                                       \
			wr_le16 (p, v);                                                                        \
	} while (0)
#define CF_W32(p, v)                                                                               \
	do                                                                                             \
	{                                                                                              \
		if (is_wiiu)                                                                               \
			wr_be32 (p, v);                                                                        \
		else                                                                                       \
			wr_le32 (p, v);                                                                        \
	} while (0)

	// Header (20 bytes)
	memcpy (out, is_wiiu ? "FFNT" : "CFNT", 4);
	if (is_wiiu)
		wr_be16 (out + 4, 0xFEFF); // BOM
	else
		wr_le16 (out + 4, 0xFEFF);
	CF_W16 (out + 6, header_size);
	CF_W32 (out + 8, is_wiiu ? 0x04000000 : 0x03000000); // version
	CF_W32 (out + 12, total_size);
	CF_W16 (out + 16, 4); // section count
	CF_W16 (out + 18, 0); // reserved

	// FINF (32 bytes) at offset header_size (0x14)
	u8 *finf = out + header_size;
	memcpy (finf, "FINF", 4);
	CF_W32 (finf + 4, 0x00000020);
	finf[8] = 1; // font_type
	finf[9] = (u8)cell_h; // line feed
	CF_W16 (finf + 10, 0); // alter char
	finf[12] = 0; // left space
	finf[13] = (u8)cell_w; // glyph width
	finf[14] = (u8)cell_w; // char width
	finf[15] = 0; // UTF-8

	uint tglp_off = header_size + 0x20;
	uint cwdh_off = tglp_off + tglp_size;
	uint cmap_off = cwdh_off + cwdh_size;

	CF_W32 (finf + 16, 0);
	CF_W32 (finf + 20, tglp_off + 8); // ptr to TGLP data
	CF_W32 (finf + 24, cwdh_off + 8); // ptr to CWDH data
	CF_W32 (finf + 28, cmap_off + 8); // ptr to CMAP data

	// TGLP at offset tglp_off
	u8 *tglp = out + tglp_off;
	memcpy (tglp, "TGLP", 4);
	CF_W32 (tglp + 4, tglp_size);
	tglp[8] = (u8)cell_w;
	tglp[9] = (u8)cell_h;
	tglp[10] = (u8)(cell_h > 2 ? cell_h - 2 : cell_h); // baseline
	tglp[11] = (u8)cell_w; // max char width
	CF_W32 (tglp + 12, tex_size); // sheet size
	CF_W16 (tglp + 16, 1); // sheet count
	CF_W16 (tglp + 18, 0); // format = RGBA8 (CTR/Cafe format 0)
	CF_W16 (tglp + 20, (u16)rows);
	CF_W16 (tglp + 22, (u16)cols);
	CF_W16 (tglp + 24, (u16)width);
	CF_W16 (tglp + 26, (u16)height);
	CF_W32 (tglp + 28, tglp_off + 0x20); // data offset
	memcpy (tglp + 0x20, tex_data, tex_size);
	FREE (tex_data);

	// CWDH at cwdh_off
	u8 *cwdh = out + cwdh_off;
	memcpy (cwdh, "CWDH", 4);
	CF_W32 (cwdh + 4, cwdh_size);
	CF_W16 (cwdh + 8, 0); // first index
	CF_W16 (cwdh + 10, (u16)(n_chars - 1)); // last index
	CF_W32 (cwdh + 12, 0); // next cwdh
	for (uint i = 0; i < n_chars; i++)
	{
		cwdh[16 + i * 3] = 0;
		cwdh[16 + i * 3 + 1] = (u8)cell_w;
		cwdh[16 + i * 3 + 2] = (u8)cell_w;
	}

	// CMAP at cmap_off
	u8 *cmap = out + cmap_off;
	memcpy (cmap, "CMAP", 4);
	CF_W32 (cmap + 4, cmap_size);
	CF_W16 (cmap + 8, 0x0020); // first char code
	CF_W16 (cmap + 10, (u16)(0x0020 + n_chars - 1)); // last char code
	CF_W16 (cmap + 12, 0); // direct mapping
	CF_W16 (cmap + 14, 0); // reserved
	CF_W32 (cmap + 16, 0); // next cmap
	CF_W32 (cmap + 20, 0); // index offset

#undef CF_W16
#undef CF_W32

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}
