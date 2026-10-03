#include "lib-std.h"
#include "lib-artetex.h"
#include <string.h>

// ArtePiazza Nintendo DS Texture Container (.tex)
// Formats:
//   1: A3I5 (3-bit alpha, 5-bit color index, up to 32 colors)
//   3: 4-bpp indexed (16 colors, 2 pixels/byte, low nibble first)
//   4: 8-bpp indexed (256 colors, 1 pixel/byte)
//   6: A5I3 (5-bit alpha, 3-bit color index, up to 8 colors)
// Palette: 16-bit little-endian RGB555 colors.

static inline u32 read_le32 (const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static inline u16 read_le16 (const u8 *p)
{
	return (u16)p[0] | ((u16)p[1] << 8);
}

bool IsArteTexture (const u8 *data, uint data_size)
{
	if (!data || data_size < 56)
		return false;

	if (data_size >= 112 && !memcmp (data, "TextureObject\0", 14))
	{
		const u32 fmt = read_le32 (data + 0x20);
		const u32 sw = read_le32 (data + 0x24);
		const u32 sh = read_le32 (data + 0x28);
		const u32 pix_len = read_le32 (data + 0x30);
		const u32 hlen = read_le32 (data + 0x34);
		const u32 plen = read_le32 (data + 0x38);
		const u32 poff = read_le32 (data + 0x3c);

		if (fmt != 1 && fmt != 3 && fmt != 4 && fmt != 6)
			return false;
		if (sw > 12 || sh > 12)
			return false;

		const uint w = 8u << sw;
		if (!w || w > 4096)
			return false;

		uint h = 0;
		if (fmt == 3)
			h = (pix_len * 2) / w;
		else
			h = pix_len / w;
		if (!h)
			h = 8u << sh;
		if (!h || h > 4096)
			return false;

		if (hlen < 112 || hlen > data_size)
			return false;
		if (!pix_len || (u64)hlen + pix_len > data_size)
			return false;
		if (poff < hlen || (u64)poff + plen > data_size)
			return false;

		return true;
	}

	if (data_size >= 56 && (!memcmp (data, "TextureData\0", 12) || !memcmp (data, "Texture Data\0", 13)))
	{
		const u32 fmt = read_le32 (data + 0x10);
		const u32 hlen = read_le32 (data + 0x14);
		const u32 pix_len = read_le32 (data + 0x2c);
		const u32 poff = read_le32 (data + 0x30);
		const u32 plen = read_le32 (data + 0x34);

		if (fmt != 1 && fmt != 3 && fmt != 4 && fmt != 6)
			return false;
		if (hlen < 56 || hlen > data_size)
			return false;
		if (!pix_len || (u64)hlen + pix_len > data_size)
			return false;

		const u64 pal_off = (u64)hlen + poff;
		if (pal_off + plen > data_size)
			return false;

		return true;
	}

	return false;
}

enumError DecodeArteTexture (
	u8 **out_rgba, uint *out_width, uint *out_height, const u8 *raw, uint raw_size)
{
	if (!out_rgba || !out_width || !out_height || !raw)
		return ERR_INVALID_DATA;
	*out_rgba = 0;
	*out_width = 0;
	*out_height = 0;

	if (!IsArteTexture (raw, raw_size))
		return ERR_NOTHING_TO_DO;

	u32 fmt = 0, sw = 0, sh = 0, pix_len = 0, hlen = 0, plen = 0, pal_off = 0;
	uint w = 0, h = 0;

	if (raw_size >= 112 && !memcmp (raw, "TextureObject\0", 14))
	{
		fmt = read_le32 (raw + 0x20);
		sw = read_le32 (raw + 0x24);
		sh = read_le32 (raw + 0x28);
		pix_len = read_le32 (raw + 0x30);
		hlen = read_le32 (raw + 0x34);
		plen = read_le32 (raw + 0x38);
		pal_off = read_le32 (raw + 0x3c);

		w = 8u << sw;
		if (fmt == 3)
			h = (pix_len * 2) / w;
		else
			h = pix_len / w;
		if (!h)
			h = 8u << sh;
	}
	else
	{
		fmt = read_le32 (raw + 0x10);
		hlen = read_le32 (raw + 0x14);
		sw = read_le32 (raw + 0x24);
		sh = read_le32 (raw + 0x28);
		pix_len = read_le32 (raw + 0x2c);
		pal_off = hlen + read_le32 (raw + 0x30);
		plen = read_le32 (raw + 0x34);

		w = (sw <= 8) ? (8u << sw) : (sw * 4);
		if (fmt == 3)
			h = (pix_len * 2) / w;
		else
			h = pix_len / w;
		if (!h)
			h = (sh <= 8) ? (8u << sh) : 32;
	}

	if (!w || !h || (u64)w * h > 4096 * 4096)
		return ERR_INVALID_DATA;
	if (hlen + pix_len > raw_size || pal_off + plen > raw_size)
		return ERR_INVALID_DATA;

	const uint max_pal_colors = plen / 2;
	if (!max_pal_colors)
		return ERR_INVALID_DATA;

	typedef struct
	{
		u8 r, g, b, a;
	} color_rgba_t;

	color_rgba_t pal[256];
	memset (pal, 0, sizeof (pal));

	const u8 *pal_raw = raw + pal_off;
	const uint load_colors = max_pal_colors < 256 ? max_pal_colors : 256;
	for (uint i = 0; i < load_colors; i++)
	{
		const u16 c = read_le16 (pal_raw + i * 2);
		const u8 r = (u8)(((c & 0x001f) * 255 + 15) / 31);
		const u8 g = (u8)((((c >> 5) & 0x001f) * 255 + 15) / 31);
		const u8 b = (u8)((((c >> 10) & 0x001f) * 255 + 15) / 31);

		pal[i].r = r;
		pal[i].g = g;
		pal[i].b = b;
		// For standard indexed modes (3 and 4), color 0 is transparent key
		pal[i].a = (i == 0) ? 0 : 255;
	}

	const u64 total_pixels = (u64)w * h;
	u8 *rgba = MALLOC (total_pixels * 4);
	if (!rgba)
		return ERR_OUT_OF_MEMORY;

	const u8 *pix = raw + hlen;

	if (fmt == 3) // 4-bpp indexed (2 pixels per byte, low nibble first)
	{
		for (u64 p = 0; p < total_pixels; p++)
		{
			const u64 byte_idx = p / 2;
			if (byte_idx >= pix_len)
			{
				memset (rgba + p * 4, 0, 4);
				continue;
			}
			const u8 b = pix[byte_idx];
			u8 idx = (p & 1) ? ((b >> 4) & 0x0f) : (b & 0x0f);
			if (idx >= load_colors)
				idx = (u8)(load_colors - 1);

			rgba[p * 4 + 0] = pal[idx].r;
			rgba[p * 4 + 1] = pal[idx].g;
			rgba[p * 4 + 2] = pal[idx].b;
			rgba[p * 4 + 3] = (idx == 0) ? 0 : 255;
		}
	}
	else if (fmt == 4) // 8-bpp indexed (1 pixel per byte)
	{
		for (u64 p = 0; p < total_pixels; p++)
		{
			if (p >= pix_len)
			{
				memset (rgba + p * 4, 0, 4);
				continue;
			}
			u8 idx = pix[p];
			if (idx >= load_colors)
				idx = (u8)(load_colors - 1);

			rgba[p * 4 + 0] = pal[idx].r;
			rgba[p * 4 + 1] = pal[idx].g;
			rgba[p * 4 + 2] = pal[idx].b;
			rgba[p * 4 + 3] = (idx == 0) ? 0 : 255;
		}
	}
	else if (fmt == 1) // A3I5 (3-bit alpha, 5-bit color index)
	{
		for (u64 p = 0; p < total_pixels; p++)
		{
			if (p >= pix_len)
			{
				memset (rgba + p * 4, 0, 4);
				continue;
			}
			const u8 val = pix[p];
			u8 idx = val & 0x1f;
			const u8 a3 = (val >> 5) & 0x07;
			const u8 alpha = (u8)((a3 * 255 + 3) / 7);
			if (idx >= load_colors)
				idx = (u8)(load_colors - 1);

			rgba[p * 4 + 0] = pal[idx].r;
			rgba[p * 4 + 1] = pal[idx].g;
			rgba[p * 4 + 2] = pal[idx].b;
			rgba[p * 4 + 3] = alpha;
		}
	}
	else if (fmt == 6) // A5I3 (5-bit alpha, 3-bit color index)
	{
		for (u64 p = 0; p < total_pixels; p++)
		{
			if (p >= pix_len)
			{
				memset (rgba + p * 4, 0, 4);
				continue;
			}
			const u8 val = pix[p];
			u8 idx = val & 0x07;
			const u8 a5 = (val >> 3) & 0x1f;
			const u8 alpha = (u8)((a5 * 255 + 15) / 31);
			if (idx >= load_colors)
				idx = (u8)(load_colors - 1);

			rgba[p * 4 + 0] = pal[idx].r;
			rgba[p * 4 + 1] = pal[idx].g;
			rgba[p * 4 + 2] = pal[idx].b;
			rgba[p * 4 + 3] = alpha;
		}
	}

	*out_rgba = rgba;
	*out_width = w;
	*out_height = h;
	return ERR_OK;
}
