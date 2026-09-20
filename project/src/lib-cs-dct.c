// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Chicken Shoot (Wii / PC) .dct stage and graphics archive support.
//-----------------------------------------------------------------------------
#include "lib-cs-dct.h"
#include "lib-std.h"
#include "lib-image.h"
#include "lib-archive-util.h"

#include <string.h>
#include <ctype.h>

static void init_cs_table (u16 table[256])
{
	for (int i = 0; i < 256; i++)
		table[i] = (u16)((i * i) / 2);
	table[1] = 1;
}

bool IsCSDCT (const u8 *data, size_t size)
{
	if (!data || size < 32)
		return false;

	const u16 version = rd_le16 (data);
	if (version != 5)
		return false;

	const u16 num_bgr = rd_le16 (data + 2);
	const u16 num_anim = rd_le16 (data + 4);
	const u16 num_obj = rd_le16 (data + 6);
	const u16 h4 = rd_le16 (data + 8);
	const u16 h5 = rd_le16 (data + 10);

	if (num_bgr == 0 || num_bgr > 32)
		return false;
	if (num_anim == 0 || num_anim > 512)
		return false;
	if (num_obj == 0 || num_obj > 16384)
		return false;
	if (h4 == 0 || h4 > 512 || h5 == 0 || h5 > 64)
		return false;

	if (16 + (size_t)num_bgr * 16 > size)
		return false;

	return true;
}

enumError DecompressCS_LZ (const u8 *src, size_t src_size, u8 *dst, size_t dst_size, size_t *produced)
{
	if (!src || !dst || dst_size == 0)
		return ERR_INVALID_DATA;

	u16 table[256];
	init_cs_table (table);

	size_t in_pos = 0;
	size_t out_pos = 0;

	while (in_pos + 1 < src_size && out_pos < dst_size)
	{
		const u8 b0 = src[in_pos++];
		const u8 b1 = src[in_pos++];

		if (b0 == 0)
		{
			const size_t count = b1;
			if (in_pos + count > src_size || out_pos + count > dst_size)
				return ERR_INVALID_DATA;
			memcpy (dst + out_pos, src + in_pos, count);
			in_pos += count;
			out_pos += count;
		}
		else
		{
			const size_t count = b1;
			const size_t offset = table[b0];
			if (offset == 0 || offset > out_pos)
				return ERR_INVALID_DATA;
			if (out_pos + count > dst_size)
				return ERR_INVALID_DATA;

			for (size_t k = 0; k < count; k++)
			{
				dst[out_pos] = dst[out_pos - offset];
				out_pos++;
			}
		}
	}

	if (produced)
		*produced = out_pos;
	return ERR_OK;
}

enumError CompressCS_LZ (const u8 *src, size_t src_size, u8 **out_data, size_t *out_size)
{
	if (!src || !out_data || !out_size)
		return ERR_INVALID_DATA;

	u16 table[256];
	init_cs_table (table);

	size_t cap = src_size * 2 + 256;
	u8 *out = MALLOC (cap);
	if (!out)
		return ERR_OUT_OF_MEMORY;

	size_t out_pos = 0;
	size_t in_pos = 0;
	u8 lit_buf[256];
	size_t lit_count = 0;

	while (in_pos < src_size)
	{
		size_t best_len = 0;
		u8 best_b0 = 0;

		for (int b0 = 1; b0 < 256; b0++)
		{
			const size_t dist = table[b0];
			if (dist > in_pos)
				continue;
			size_t len = 0;
			while (in_pos + len < src_size && len < 255
				&& src[in_pos + len] == src[in_pos + len - dist])
			{
				len++;
			}
			if (len > best_len)
			{
				best_len = len;
				best_b0 = (u8)b0;
				if (len == 255)
					break;
			}
		}

		if (best_len >= 3)
		{
			if (lit_count > 0)
			{
				out[out_pos++] = 0;
				out[out_pos++] = (u8)lit_count;
				memcpy (out + out_pos, lit_buf, lit_count);
				out_pos += lit_count;
				lit_count = 0;
			}
			out[out_pos++] = best_b0;
			out[out_pos++] = (u8)best_len;
			in_pos += best_len;
		}
		else
		{
			lit_buf[lit_count++] = src[in_pos++];
			if (lit_count == 255)
			{
				out[out_pos++] = 0;
				out[out_pos++] = 255;
				memcpy (out + out_pos, lit_buf, 255);
				out_pos += 255;
				lit_count = 0;
			}
		}
	}

	if (lit_count > 0)
	{
		out[out_pos++] = 0;
		out[out_pos++] = (u8)lit_count;
		memcpy (out + out_pos, lit_buf, lit_count);
		out_pos += lit_count;
	}

	*out_data = out;
	*out_size = out_pos;
	return ERR_OK;
}

static void decode_rgb555_palette (const u8 *pal_raw, u8 rgba_pal[256][4])
{
	if (!pal_raw)
	{
		for (int i = 0; i < 256; i++)
		{
			rgba_pal[i][0] = (u8)i;
			rgba_pal[i][1] = (u8)i;
			rgba_pal[i][2] = (u8)i;
			rgba_pal[i][3] = 255;
		}
		return;
	}

	for (int i = 0; i < 256; i++)
	{
		const u16 col = rd_be16 (pal_raw + i * 2);
		if (col == 0)
		{
			rgba_pal[i][0] = 0;
			rgba_pal[i][1] = 0;
			rgba_pal[i][2] = 0;
			rgba_pal[i][3] = 0;
		}
		else
		{
			rgba_pal[i][0] = (u8)(((col >> 10) & 0x1f) * 255 / 31);
			rgba_pal[i][1] = (u8)(((col >> 5) & 0x1f) * 255 / 31);
			rgba_pal[i][2] = (u8)((col & 0x1f) * 255 / 31);
			rgba_pal[i][3] = 255;
		}
	}
}

static u8 *indices_to_rgba (const u8 *indices, uint w, uint h, const u8 rgba_pal[256][4])
{
	const size_t num_pixels = (size_t)w * h;
	u8 *rgba = MALLOC (num_pixels * 4);
	if (!rgba)
		return 0;

	for (size_t p = 0; p < num_pixels; p++)
	{
		const u8 idx = indices[p];
		rgba[p * 4 + 0] = rgba_pal[idx][0];
		rgba[p * 4 + 1] = rgba_pal[idx][1];
		rgba[p * 4 + 2] = rgba_pal[idx][2];
		rgba[p * 4 + 3] = rgba_pal[idx][3];
	}
	return rgba;
}

static void sanitize_name (char *dst, size_t dst_size, const char *src)
{
	size_t d = 0;
	while (*src && d + 1 < dst_size)
	{
		const unsigned char c = (unsigned char)*src++;
		if (isalnum (c) || c == '_' || c == '-')
			dst[d++] = (char)c;
		else if (c == ' ')
			dst[d++] = '_';
	}
	dst[d] = 0;
	if (d == 0)
		snprintf (dst, dst_size, "unnamed");
}

enumError DecodeCSDCT_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsCSDCT (data, size))
		return ERR_INVALID_DATA;

	const u16 version = rd_le16 (data);
	const u16 num_bgr = rd_le16 (data + 2);
	const u16 num_anim = rd_le16 (data + 4);
	const u16 num_obj = rd_le16 (data + 6);
	const u16 h4 = rd_le16 (data + 8);
	const u16 h5 = rd_le16 (data + 10);
	const u16 h6 = rd_le16 (data + 12);
	const u16 h7 = rd_le16 (data + 14);

	fprintf (out, "# Chicken Shoot DCT Stage Archive\n");
	fprintf (out, "version: %u\n", version);
	fprintf (out, "background_count: %u\n", num_bgr);
	fprintf (out, "animation_count: %u\n", num_anim);
	fprintf (out, "object_count: %u\n", num_obj);
	fprintf (out, "dimensions: h4=%u, h5=%u, h6=%u, h7=%u\n\n", h4, h5, h6, h7);

	size_t pos = 16;
	fprintf (out, "## Backgrounds\n");
	for (uint i = 0; i < num_bgr; i++)
	{
		const u16 unk0 = rd_le16 (data + pos);
		const u16 w = rd_le16 (data + pos + 2);
		const u16 h = rd_le16 (data + pos + 4);
		const u16 flags = rd_le16 (data + pos + 6);
		const u16 pal_flag = rd_le16 (data + pos + 8);
		const u16 aux_flag = rd_le16 (data + pos + 10);
		fprintf (out, "  bgr[%u]: %ux%u (flags=0x%04x, palette=%s, aux=%u, unk=0x%04x)\n",
			i, w, h, flags, pal_flag ? "yes" : "no", aux_flag, unk0);
		pos += 16;
	}

	return ERR_OK;
}

enumError ExtractCSDCTArchive (ccp arg, ccp basedir, uint depth, const u8 *data, size_t size)
{
	if (!IsCSDCT (data, size))
		return ERR_INVALID_DATA;

	const u16 version = rd_le16 (data);
	const u16 num_bgr = rd_le16 (data + 2);
	const u16 num_anim = rd_le16 (data + 4);
	const u16 num_obj = rd_le16 (data + 6);
	const u16 h4 = rd_le16 (data + 8);
	const u16 h5 = rd_le16 (data + 10);

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	char manifest_path[PATH_MAX];
	snprintf (manifest_path, sizeof (manifest_path), "%s/manifest.json", dest);
	FILE *fm = fopen (manifest_path, "w");
	if (fm)
	{
		fprintf (fm, "{\n");
		fprintf (fm, "  \"format\": \"ChickenShoot_DCT\",\n");
		fprintf (fm, "  \"version\": %u,\n", version);
		fprintf (fm, "  \"background_count\": %u,\n", num_bgr);
		fprintf (fm, "  \"animation_count\": %u,\n", num_anim);
		fprintf (fm, "  \"object_count\": %u,\n", num_obj);
		fprintf (fm, "  \"h4\": %u,\n", h4);
		fprintf (fm, "  \"h5\": %u,\n", h5);
	}

	// Read background records
	size_t pos = 16;
	typedef struct {
		u16 w, h, flags, pal_flag, aux_flag;
	} bgr_rec_t;
	bgr_rec_t bgrs[32];
	for (uint i = 0; i < num_bgr; i++)
	{
		bgrs[i].w = rd_le16 (data + pos + 2);
		bgrs[i].h = rd_le16 (data + pos + 4);
		bgrs[i].flags = rd_le16 (data + pos + 6);
		bgrs[i].pal_flag = rd_le16 (data + pos + 8);
		bgrs[i].aux_flag = rd_le16 (data + pos + 10);
		pos += 16;
	}

	char bgr_dir[PATH_MAX];
	snprintf (bgr_dir, sizeof (bgr_dir), "%s/backgrounds", dest);
	CreatePath (bgr_dir, true);

	if (fm)
		fprintf (fm, "  \"backgrounds\": [\n");

	// Process backgrounds
	for (uint i = 0; i < num_bgr; i++)
	{
		const uint w = bgrs[i].w;
		const uint h = bgrs[i].h;
		const bool has_data = (bgrs[i].flags & 1) != 0;
		const bool has_pal = bgrs[i].pal_flag != 0;

		u8 *dec_pixels = 0;
		if (has_data && w > 0 && h > 0 && pos + 4 <= size)
		{
			const u32 clen = rd_le32 (data + pos);
			pos += 4;
			if (pos + clen <= size)
			{
				dec_pixels = MALLOC ((size_t)w * h);
				if (dec_pixels)
				{
					size_t produced = 0;
					DecompressCS_LZ (data + pos, clen, dec_pixels, (size_t)w * h, &produced);
				}
				pos += clen;
			}
		}

		const u8 *pal_raw = 0;
		if (has_pal && pos + 512 <= size)
		{
			pal_raw = data + pos;
			pos += 512;
		}

		if (bgrs[i].aux_flag != 0 && pos + 4 <= size)
		{
			const u32 aux_clen = rd_le32 (data + pos);
			pos += 4 + aux_clen;
		}

		if (dec_pixels)
		{
			u8 rgba_pal[256][4];
			decode_rgb555_palette (pal_raw, rgba_pal);
			u8 *rgba = indices_to_rgba (dec_pixels, w, h, rgba_pal);
			FREE (dec_pixels);

			if (rgba)
			{
				char png_path[PATH_MAX];
				snprintf (png_path, sizeof (png_path), "%s/bgr_%02u_%ux%u.png", bgr_dir, i, w, h);
				SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png_path, 0, true);
			}
		}

		if (fm)
		{
			fprintf (fm, "    { \"index\": %u, \"width\": %u, \"height\": %u, \"has_palette\": %s }%s\n",
				i, w, h, has_pal ? "true" : "false", (i + 1 < num_bgr) ? "," : "");
		}
	}

	if (fm)
		fprintf (fm, "  ],\n  \"animations\": [\n");

	char anim_dir[PATH_MAX];
	snprintf (anim_dir, sizeof (anim_dir), "%s/animations", dest);
	CreatePath (anim_dir, true);

	// Process animations
	for (uint i = 0; i < num_anim && pos + 138 <= size; i++)
	{
		const u16 aid = rd_le16 (data + pos);
		const u16 aw = rd_le16 (data + pos + 2);
		const u16 ah = rd_le16 (data + pos + 4);
		const u16 aframes = rd_le16 (data + pos + 6);
		const u16 aflags = rd_le16 (data + pos + 8);
		pos += 10;

		char raw_name[129];
		memcpy (raw_name, data + pos, 128);
		raw_name[128] = 0;
		pos += 128;

		char name[64];
		sanitize_name (name, sizeof (name), raw_name);

		const bool has_data = (aflags & 1) != 0;
		const u8 *pal_raw = 0;
		if (has_data && pos + 512 <= size)
		{
			pal_raw = data + pos;
			pos += 512;
		}

		u8 *dec_frames = 0;
		const size_t total_pixels = (size_t)aw * ah * aframes;
		if (has_data && total_pixels > 0 && pos + 4 <= size)
		{
			const u32 clen = rd_le32 (data + pos);
			pos += 4;
			if (pos + clen <= size)
			{
				dec_frames = MALLOC (total_pixels);
				if (dec_frames)
				{
					size_t produced = 0;
					DecompressCS_LZ (data + pos, clen, dec_frames, total_pixels, &produced);
				}
				pos += clen;
			}
		}

		if (dec_frames)
		{
			u8 rgba_pal[256][4];
			decode_rgb555_palette (pal_raw, rgba_pal);

			char cur_anim_dir[PATH_MAX];
			snprintf (cur_anim_dir, sizeof (cur_anim_dir), "%s/%02u_%s", anim_dir, i, name);
			CreatePath (cur_anim_dir, true);

			const size_t frame_pixels = (size_t)aw * ah;
			for (uint f = 0; f < aframes; f++)
			{
				u8 *rgba = indices_to_rgba (dec_frames + f * frame_pixels, aw, ah, rgba_pal);
				if (rgba)
				{
					char frame_png[PATH_MAX];
					snprintf (frame_png, sizeof (frame_png), "%s/frame_%03u.png", cur_anim_dir, f);
					SaveDecodedRGBAToPNG (rgba, aw, ah, &be_func, frame_png, 0, true);
				}
			}
			FREE (dec_frames);
		}

		if (fm)
		{
			fprintf (fm, "    { \"index\": %u, \"id\": %u, \"name\": \"%s\", \"width\": %u, \"height\": %u, \"frames\": %u }%s\n",
				i, aid, name, aw, ah, aframes, (i + 1 < num_anim) ? "," : "");
		}
	}

	if (fm)
		fprintf (fm, "  ]\n}\n");

	if (fm)
		fclose (fm);

	// Process objects
	char obj_dir[PATH_MAX];
	snprintf (obj_dir, sizeof (obj_dir), "%s/objects", dest);
	CreatePath (obj_dir, true);

	if (pos + 4 <= size)
	{
		const u32 clen = rd_le32 (data + pos);
		pos += 4;
		if (pos + clen <= size)
		{
			const size_t obj_size = (size_t)num_obj * 2;
			u8 *dec_obj = MALLOC (obj_size);
			if (dec_obj)
			{
				size_t produced = 0;
				DecompressCS_LZ (data + pos, clen, dec_obj, obj_size, &produced);
				char obj_path[PATH_MAX];
				snprintf (obj_path, sizeof (obj_path), "%s/objects_main.bin", obj_dir);
				SaveFile (obj_path, 0, 0, dec_obj, (uint)produced, 0);
				FREE (dec_obj);
			}
			pos += clen;
		}
	}

	uint extra_idx = 0;
	while (pos + 4 <= size)
	{
		const u32 clen = rd_le32 (data + pos);
		if (pos + 4 + clen > size)
			break;
		pos += 4;
		u8 *dec_extra = MALLOC (128);
		if (dec_extra)
		{
			size_t produced = 0;
			DecompressCS_LZ (data + pos, clen, dec_extra, 128, &produced);
			char extra_path[PATH_MAX];
			snprintf (extra_path, sizeof (extra_path), "%s/extra_%03u.bin", obj_dir, extra_idx++);
			SaveFile (extra_path, 0, 0, dec_extra, (uint)produced, 0);
			FREE (dec_extra);
		}
		pos += clen;
	}

	return ERR_OK;
}
