#include "lib-plt0.h"
#include <stdlib.h>
#include <string.h>

// PLT0 (Brawl/G3D palette-animation-adjacent palette resource, embedded in
// BRRES/.pac). Layout cross-checked field-by-field against BrawlLib's own
// struct definitions (libertyernie/BrawlCrate, BrawlLib/SSBB/Types/PLT0.cs
// PLT0v1/PLT0v3, and Wii/Textures/Enum.cs for the WiiPaletteFormat values):
// BRESCommonHeader tag/size/version/bresOffset (4x u32) at 0x00, then
// _headerLen (u32, offset to palette data -- always 0x40 in practice, both
// PLT0v1 and PLT0v3 pad their extra fields to the same header size) at 0x10,
// _stringOffset (u32) at 0x14, _pixelFormat (u32, WiiPaletteFormat: IA8=0,
// RGB565=1, RGB5A3=2) at 0x18, _numEntries (s16) at 0x1c. Verified against a
// real retail PLT0 (a.tex.plt0, Brawl RUUE NPC skin palettes found on disk):
// headerLen=0x40, pixelFormat=1 (RGB565), numEntries=16, matching the file's
// 0x60-byte total size (0x40 header + 16*2 palette bytes) exactly.
enumError LoadPLT0 (Image_t *img, const u8 *data, uint data_size)
{
	if (!data || data_size < 0x20 || memcmp (data, "PLT0", 4) != 0)
		return ERR_INVALID_DATA;

	u32 pal_offset = (data[0x10] << 24) | (data[0x11] << 16) | (data[0x12] << 8) | data[0x13];
	u32 pform = (data[0x18] << 24) | (data[0x19] << 16) | (data[0x1A] << 8) | data[0x1B];
	u16 num_colors = (data[0x1C] << 8) | data[0x1D];

	if (!num_colors || (u64)pal_offset + (u64)num_colors * 2 > data_size)
		return ERR_WARNING;

	// IMG_RGBA32 is the tiled 4x4-block Wii texture format (GetImageGeometry()
	// rejects any width/height not divisible by 4, which a 1-high palette
	// strip never is); IMG_X_RGB is the flat untiled RGBA raster this
	// codebase already uses for X_GRAY/X_RGB "not a real console format"
	// dumps, so it's the right target for a palette-as-a-strip PNG.
	//
	// SavePNG() indexes img->data using an xwidth/xheight (EXPAND8-rounded)
	// stride, not width/height -- same padding requirement as the BFLIM/FLIM
	// decode path just above (DecodeFLIM_RGBA branch), which this mirrors.
	const uint xwidth = EXPAND8 (num_colors), xheight = EXPAND8 (1);
	img->iform = IMG_X_RGB;
	img->width = num_colors;
	img->xwidth = xwidth;
	img->height = 1;
	img->xheight = xheight;
	img->data_size = xwidth * xheight * 4;
	img->data = CALLOC (1, img->data_size);
	if (!img->data)
		return ERR_OUT_OF_MEMORY;
	img->data_alloced = true;

	if (pform == 0)
		img->pform = PAL_IA8;
	else if (pform == 1)
		img->pform = PAL_RGB565;
	else if (pform == 2)
		img->pform = PAL_RGB5A3;
	else
		img->pform = PAL_INVALID;

	const u8 *p = data + pal_offset;
	u8 *d = img->data;

	for (u16 i = 0; i < num_colors; i++)
	{
		u16 c = (p[0] << 8) | p[1];
		p += 2;
		u8 r, g, b, a;

		if (pform == 0) // IA8
		{
			a = c >> 8;
			u8 intensity = c & 0xFF;
			r = g = b = intensity;
		}
		else if (pform == 1) // RGB565
		{
			r = (c >> 11) & 0x1F;
			r = (r << 3) | (r >> 2);
			g = (c >> 5) & 0x3F;
			g = (g << 2) | (g >> 4);
			b = c & 0x1F;
			b = (b << 3) | (b >> 2);
			a = 0xFF;
		}
		else if (pform == 2) // RGB5A3
		{
			if (c & 0x8000)
			{
				r = (c >> 10) & 0x1F;
				r = (r << 3) | (r >> 2);
				g = (c >> 5) & 0x1F;
				g = (g << 3) | (g >> 2);
				b = c & 0x1F;
				b = (b << 3) | (b >> 2);
				a = 0xFF;
			}
			else
			{
				a = (c >> 12) & 0x07;
				a = (a << 5) | (a << 2) | (a >> 1);
				r = (c >> 8) & 0x0F;
				r = (r << 4) | r;
				g = (c >> 4) & 0x0F;
				g = (g << 4) | g;
				b = c & 0x0F;
				b = (b << 4) | b;
			}
		}
		else
		{
			r = g = b = a = 0;
		}

		*d++ = r;
		*d++ = g;
		*d++ = b;
		*d++ = a;
	}

	return ERR_OK;
}

bool GetRawPLT0 (
	const u8 *data, uint data_size, palette_format_t *pform, uint *n_colors, const u8 **pal_ptr)
{
	if (!data || data_size < 0x20 || memcmp (data, "PLT0", 4) != 0)
		return false;

	u32 pal_offset = (data[0x10] << 24) | (data[0x11] << 16) | (data[0x12] << 8) | data[0x13];
	u32 raw_pform = (data[0x18] << 24) | (data[0x19] << 16) | (data[0x1A] << 8) | data[0x1B];
	u16 num_colors = (data[0x1C] << 8) | data[0x1D];

	if (!num_colors || (u64)pal_offset + (u64)num_colors * 2 > data_size)
		return false;

	switch (raw_pform)
	{
		case 0:
			*pform = PAL_IA8;
			break;
		case 1:
			*pform = PAL_RGB565;
			break;
		case 2:
			*pform = PAL_RGB5A3;
			break;
		default:
			return false;
	}

	*n_colors = num_colors;
	*pal_ptr = data + pal_offset;
	return true;
}

enumError EncodePLT0_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint num_colors, palette_format_t pform)
{
	if (!dest || !dest_size || !rgba || !num_colors)
		return ERR_SEMANTIC;
	if (num_colors > 65535)
		num_colors = 65535;

	u32 raw_pform = 2; // default RGB5A3
	if (pform == PAL_IA8)
		raw_pform = 0;
	else if (pform == PAL_RGB565)
		raw_pform = 1;
	else if (pform == PAL_RGB5A3)
		raw_pform = 2;
	else
	{
		// Auto-detect format based on alpha
		bool has_trans = false;
		for (uint i = 0; i < num_colors; i++)
		{
			if (rgba[i * 4 + 3] < 224)
			{
				has_trans = true;
				break;
			}
		}
		raw_pform = has_trans ? 2 : 1;
	}

	uint total_size = 0x40 + num_colors * 2;
	u8 *out = CALLOC (1, total_size);
	if (!out)
		return ERR_OUT_OF_MEMORY;

	memcpy (out, "PLT0", 4);
	out[0x04] = (u8)(total_size >> 24);
	out[0x05] = (u8)(total_size >> 16);
	out[0x06] = (u8)(total_size >> 8);
	out[0x07] = (u8)(total_size & 0xFF);
	out[0x0B] = 1; // version 1
	out[0x13] = 0x40; // headerLen (0x00000040)
	out[0x1B] = (u8)raw_pform; // pixelFormat
	out[0x1C] = (u8)(num_colors >> 8);
	out[0x1D] = (u8)(num_colors & 0xFF);

	u8 *pal = out + 0x40;
	for (uint i = 0; i < num_colors; i++)
	{
		u8 r = rgba[i * 4];
		u8 g = rgba[i * 4 + 1];
		u8 b = rgba[i * 4 + 2];
		u8 a = rgba[i * 4 + 3];
		u16 c;

		if (raw_pform == 0) // IA8
		{
			u8 intensity = (u8)(((u32)r * 77 + (u32)g * 150 + (u32)b * 29) >> 8);
			c = ((u16)a << 8) | intensity;
		}
		else if (raw_pform == 1) // RGB565
		{
			c = ((u16)(r >> 3) << 11) | ((u16)(g >> 2) << 5) | (u16)(b >> 3);
		}
		else // RGB5A3
		{
			if (a < 224)
				c = ((u16)(a >> 5) << 12) | ((u16)(r >> 4) << 8) | ((u16)(g >> 4) << 4)
					| (u16)(b >> 4);
			else
				c = 0x8000 | ((u16)(r >> 3) << 10) | ((u16)(g >> 3) << 5) | (u16)(b >> 3);
		}

		pal[i * 2] = (u8)(c >> 8);
		pal[i * 2 + 1] = (u8)(c & 0xFF);
	}

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}

enumError EncodePLT0_Raw (
	u8 **dest, uint *dest_size, const u8 *raw_pal, uint num_colors, palette_format_t pform)
{
	if (!dest || !dest_size || !raw_pal || !num_colors)
		return ERR_SEMANTIC;
	if (num_colors > 65535)
		num_colors = 65535;

	u32 raw_pform;
	switch (pform)
	{
		case PAL_IA8:
			raw_pform = 0;
			break;
		case PAL_RGB565:
			raw_pform = 1;
			break;
		case PAL_RGB5A3:
			raw_pform = 2;
			break;
		default:
			return ERR_SEMANTIC;
	}

	const uint total_size = 0x40 + num_colors * 2;
	u8 *out = CALLOC (1, total_size);
	if (!out)
		return ERR_OUT_OF_MEMORY;

	memcpy (out, "PLT0", 4);
	out[0x04] = (u8)(total_size >> 24);
	out[0x05] = (u8)(total_size >> 16);
	out[0x06] = (u8)(total_size >> 8);
	out[0x07] = (u8)total_size;
	out[0x0B] = 1; // version 1
	out[0x13] = 0x40; // headerLen
	out[0x1B] = (u8)raw_pform;
	out[0x1C] = (u8)(num_colors >> 8);
	out[0x1D] = (u8)num_colors;
	memcpy (out + 0x40, raw_pal, num_colors * 2);

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}

typedef struct plt0_hist_t
{
	u16 color16;
	u32 count;
} plt0_hist_t;

static int compare_plt0_hist (const void *a, const void *b)
{
	const plt0_hist_t *ha = (const plt0_hist_t *)a;
	const plt0_hist_t *hb = (const plt0_hist_t *)b;
	if (hb->count != ha->count)
		return (hb->count > ha->count) ? 1 : -1;
	return (int)ha->color16 - (int)hb->color16;
}

static inline u16 rgba_to_pal16_val (u8 r, u8 g, u8 b, u8 a, u32 raw_pform)
{
	if (raw_pform == 0) // IA8
	{
		u8 intensity = (a == 0) ? 0 : (u8)(((u32)r * 77 + (u32)g * 150 + (u32)b * 29) >> 8);
		return ((u16)a << 8) | intensity;
	}
	else if (raw_pform == 1) // RGB565
	{
		return ((u16)(r >> 3) << 11) | ((u16)(g >> 2) << 5) | (u16)(b >> 3);
	}
	else // RGB5A3
	{
		if (a < 224)
			return ((u16)(a >> 5) << 12) | ((u16)(r >> 4) << 8) | ((u16)(g >> 4) << 4)
				| (u16)(b >> 4);
		else
			return 0x8000 | ((u16)(r >> 3) << 10) | ((u16)(g >> 3) << 5) | (u16)(b >> 3);
	}
}

static inline void pal16_val_to_rgba (u16 c, u32 raw_pform, u8 *r, u8 *g, u8 *b, u8 *a)
{
	if (raw_pform == 0) // IA8
	{
		*a = (u8)(c >> 8);
		*r = *g = *b = (u8)(c & 0xFF);
	}
	else if (raw_pform == 1) // RGB565
	{
		u8 cr = (c >> 11) & 0x1F;
		*r = (cr << 3) | (cr >> 2);
		u8 cg = (c >> 5) & 0x3F;
		*g = (cg << 2) | (cg >> 4);
		u8 cb = c & 0x1F;
		*b = (cb << 3) | (cb >> 2);
		*a = 0xFF;
	}
	else // RGB5A3
	{
		if (c & 0x8000)
		{
			u8 cr = (c >> 10) & 0x1F;
			*r = (cr << 3) | (cr >> 2);
			u8 cg = (c >> 5) & 0x1F;
			*g = (cg << 3) | (cg >> 2);
			u8 cb = c & 0x1F;
			*b = (cb << 3) | (cb >> 2);
			*a = 0xFF;
		}
		else
		{
			u8 ca = (c >> 12) & 0x07;
			*a = (ca << 5) | (ca << 2) | (ca >> 1);
			u8 cr = (c >> 8) & 0x0F;
			*r = (cr << 4) | cr;
			u8 cg = (c >> 4) & 0x0F;
			*g = (cg << 4) | cg;
			u8 cb = c & 0x0F;
			*b = (cb << 4) | cb;
		}
	}
}

static inline int pal16_dist_val (u16 c1, u16 c2, u32 raw_pform)
{
	u8 r1, g1, b1, a1, r2, g2, b2, a2;
	pal16_val_to_rgba (c1, raw_pform, &r1, &g1, &b1, &a1);
	pal16_val_to_rgba (c2, raw_pform, &r2, &g2, &b2, &a2);
	return abs ((int)r1 - (int)r2) + abs ((int)g1 - (int)g2) + abs ((int)b1 - (int)b2)
		+ abs ((int)a1 - (int)a2);
}

enumError QuantizePalette_PLT0 (const u8 *rgba, uint width, uint height, uint xwidth,
	palette_format_t pform, uint target_colors, u8 **out_raw_pal, uint *out_pal_size,
	uint *out_num_colors, u16 **out_indices)
{
	if (!rgba || !width || !height || !target_colors)
		return ERR_SEMANTIC;

	u32 raw_pform = 2; // RGB5A3
	if (pform == PAL_IA8)
		raw_pform = 0;
	else if (pform == PAL_RGB565)
		raw_pform = 1;
	else if (pform == PAL_RGB5A3)
		raw_pform = 2;
	else
	{
		bool is_gray = true;
		bool has_trans = false;
		for (uint y = 0; y < height; y++)
		{
			for (uint x = 0; x < width; x++)
			{
				const u8 *p = rgba + 4 * (y * xwidth + x);
				if (p[0] != p[1] || p[1] != p[2])
					is_gray = false;
				if (p[3] < 224)
					has_trans = true;
			}
		}
		if (is_gray)
			raw_pform = 0; // IA8
		else if (has_trans)
			raw_pform = 2; // RGB5A3
		else
			raw_pform = 1; // RGB565
	}

	u32 *hist = CALLOC (65536, sizeof (u32));
	if (!hist)
		return ERR_OUT_OF_MEMORY;

	for (uint y = 0; y < height; y++)
	{
		for (uint x = 0; x < width; x++)
		{
			const u8 *p = rgba + 4 * (y * xwidth + x);
			u16 c = rgba_to_pal16_val (p[0], p[1], p[2], p[3], raw_pform);
			hist[c]++;
		}
	}

	uint unique_count = 0;
	for (uint i = 0; i < 65536; i++)
	{
		if (hist[i] > 0)
			unique_count++;
	}

	plt0_hist_t *entries = MALLOC (unique_count * sizeof (plt0_hist_t));
	if (!entries)
	{
		FREE (hist);
		return ERR_OUT_OF_MEMORY;
	}

	uint idx = 0;
	for (uint i = 0; i < 65536; i++)
	{
		if (hist[i] > 0)
		{
			entries[idx].color16 = (u16)i;
			entries[idx].count = hist[i];
			idx++;
		}
	}
	FREE (hist);

	qsort (entries, unique_count, sizeof (plt0_hist_t), compare_plt0_hist);

	u16 *pal = CALLOC (target_colors, sizeof (u16));
	if (!pal)
	{
		FREE (entries);
		return ERR_OUT_OF_MEMORY;
	}

	uint pal_count = 0;

	if (unique_count <= target_colors)
	{
		for (uint i = 0; i < unique_count; i++)
			pal[pal_count++] = entries[i].color16;
	}
	else
	{
		// Pass 1: diversity >= 10 (yoshakami/plt0 quantization)
		const int diversity1 = 10;
		for (uint i = 0; i < unique_count && pal_count < target_colors; i++)
		{
			u16 c = entries[i].color16;
			bool ok = true;
			for (uint j = 0; j < pal_count; j++)
			{
				if (pal16_dist_val (c, pal[j], raw_pform) < diversity1)
				{
					ok = false;
					break;
				}
			}
			if (ok)
				pal[pal_count++] = c;
		}

		// Pass 2: diversity >= 4 if needed
		if (pal_count < target_colors)
		{
			const int diversity2 = 4;
			for (uint i = 0; i < unique_count && pal_count < target_colors; i++)
			{
				u16 c = entries[i].color16;
				bool ok = true;
				for (uint j = 0; j < pal_count; j++)
				{
					if (pal16_dist_val (c, pal[j], raw_pform) < diversity2)
					{
						ok = false;
						break;
					}
				}
				if (ok)
					pal[pal_count++] = c;
			}
		}

		// Pass 3: Fill remainder with most used non-duplicates
		if (pal_count < target_colors)
		{
			for (uint i = 0; i < unique_count && pal_count < target_colors; i++)
			{
				u16 c = entries[i].color16;
				bool ok = true;
				for (uint j = 0; j < pal_count; j++)
				{
					if (c == pal[j])
					{
						ok = false;
						break;
					}
				}
				if (ok)
					pal[pal_count++] = c;
			}
		}
	}
	FREE (entries);

	// Pre-unpack palette RGBA for fast Manhattan distance matching
	u8 *pal_r = MALLOC (target_colors);
	u8 *pal_g = MALLOC (target_colors);
	u8 *pal_b = MALLOC (target_colors);
	u8 *pal_a = MALLOC (target_colors);
	for (uint i = 0; i < target_colors; i++)
		pal16_val_to_rgba (pal[i], raw_pform, pal_r + i, pal_g + i, pal_b + i, pal_a + i);

	if (out_indices)
	{
		u16 *indices = CALLOC (xwidth * height, sizeof (u16));
		if (!indices)
		{
			FREE (pal);
			FREE (pal_r);
			FREE (pal_g);
			FREE (pal_b);
			FREE (pal_a);
			return ERR_OUT_OF_MEMORY;
		}

		for (uint y = 0; y < height; y++)
		{
			for (uint x = 0; x < width; x++)
			{
				const u8 *p = rgba + 4 * (y * xwidth + x);
				u8 pr = p[0], pg = p[1], pb = p[2], pa = p[3];
				int min_d = 999999;
				uint best_k = 0;
				for (uint k = 0; k < pal_count; k++)
				{
					int d = abs ((int)pr - (int)pal_r[k]) + abs ((int)pg - (int)pal_g[k])
						+ abs ((int)pb - (int)pal_b[k]) + abs ((int)pa - (int)pal_a[k]);
					if (d < min_d)
					{
						min_d = d;
						best_k = k;
						if (d == 0)
							break;
					}
				}
				indices[y * xwidth + x] = (u16)best_k;
			}
		}
		*out_indices = indices;
	}

	FREE (pal_r);
	FREE (pal_g);
	FREE (pal_b);
	FREE (pal_a);

	if (out_raw_pal && out_pal_size)
	{
		u8 *raw_pal = MALLOC (target_colors * 2);
		if (!raw_pal)
		{
			FREE (pal);
			return ERR_OUT_OF_MEMORY;
		}
		for (uint i = 0; i < target_colors; i++)
		{
			raw_pal[i * 2] = (u8)(pal[i] >> 8);
			raw_pal[i * 2 + 1] = (u8)(pal[i] & 0xFF);
		}
		*out_raw_pal = raw_pal;
		*out_pal_size = target_colors * 2;
	}
	FREE (pal);

	if (out_num_colors)
		*out_num_colors = target_colors;

	return ERR_OK;
}
