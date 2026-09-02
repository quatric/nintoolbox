#include "lib-std.h"
#include "lib-ctpk.h"
#include "lib-flim.h"
#include <string.h>
#include <errno.h>

enumError ScanCTPK (nintendo_ctpk_t *ctpk, const u8 *data, uint size)
{
	if (!ctpk || !data || size < 0x20 || memcmp (data, "CTPK", 4))
		return EINVAL;
	memset (ctpk, 0, sizeof (*ctpk));
	ctpk->data = data;
	ctpk->size = size;
	ctpk->version = rd_le16 (data + 4);
	ctpk->n_entries = rd_le16 (data + 6);
	ctpk->texture_offset = rd_le32 (data + 8);
	ctpk->texture_size = rd_le32 (data + 12);
	if (ctpk->texture_offset > size || ctpk->texture_size > size - ctpk->texture_offset)
		return EINVAL;
	if (0x20 + 0x20 * (u64)ctpk->n_entries > size)
		return EINVAL;
	return ERR_OK;
}

enumError GetCTPKEntry (const nintendo_ctpk_t *ctpk, uint index, nintendo_ctpk_entry_t *entry)
{
	if (!ctpk || !ctpk->data || index >= ctpk->n_entries || !entry)
		return EINVAL;
	memset (entry, 0, sizeof (*entry));
	const u8 *info = ctpk->data + 0x20 + 0x20 * index;
	const u32 path_off = rd_le32 (info);
	const u32 data_size = rd_le32 (info + 4);
	const u32 data_off = rd_le32 (info + 8);
	const u32 fmt = rd_le32 (info + 12);
	const u16 w = rd_le16 (info + 16);
	const u16 h = rd_le16 (info + 18);
	const u8 mip = info[20];
	const u8 type = info[21];

	if (path_off > 0 && path_off < ctpk->size)
	{
		const u8 *str = ctpk->data + path_off;
		const u8 *nul = memchr (str, 0, ctpk->size - path_off);
		if (nul)
		{
			size_t len = nul - str;
			if (len >= sizeof (entry->name))
				len = sizeof (entry->name) - 1;
			memcpy (entry->name, str, len);
			entry->name[len] = 0;
		}
	}

	const u64 abs_data_off = (u64)ctpk->texture_offset + data_off;
	if (abs_data_off > ctpk->size || data_size > ctpk->size - abs_data_off)
		return EINVAL;

	entry->width = w;
	entry->height = h;
	entry->format = fmt;
	entry->mip_level = mip ? mip : 1;
	entry->type = type;
	entry->data = ctpk->data + abs_data_off;
	entry->data_size = data_size;
	return ERR_OK;
}

enumError DecodePicaTexture (
	u8 **dest, uint *width, uint *height, const u8 *src, uint w, uint h, uint format, uint src_size)
{
	if (!dest || !width || !height || !src)
		return EINVAL;
	const uint fmt = format;
	const uint data_size = src_size;

	if (!w || !h || w > 16384 || h > 16384)
		return EINVAL;
	if ((u64)w * h > NFMT_MAX_OUTPUT / 4)
		return EINVAL;

	if (fmt == 12 || fmt == 13)
	{
		u8 *rgba = MALLOC (w * h * 4);
		if (!rgba)
			return ERR_CANT_CREATE;
		enumError err = (fmt == 13) ? decode_etc1a4_tiled (rgba, src, w, h, data_size)
									: decode_etc1_tiled (rgba, src, w, h, data_size);
		if (err)
		{
			FREE (rgba);
			return err;
		}
		*dest = rgba;
		*width = w;
		*height = h;
		return ERR_OK;
	}

	const bool nibble_fmt = (fmt == 10 || fmt == 11);
	uint bpp = 0;
	switch (fmt)
	{
		case 0:
			bpp = 4;
			break;
		case 1:
			bpp = 3;
			break;
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
			bpp = 2;
			break;
		case 7:
		case 8:
		case 9:
			bpp = 1;
			break;
		case 10:
		case 11:
			bpp = 0;
			break;
		default:
			return EINVAL;
	}

	const uint tw = (w + 7) & ~7u, th = (h + 7) & ~7u;
	const u64 need = nibble_fmt ? ((u64)tw * th + 1) / 2 : (u64)tw * th * bpp;
	if (need > data_size)
		return EINVAL;

	u8 *rgba = MALLOC (w * h * 4);
	if (!rgba)
		return ERR_CANT_CREATE;

	for (uint y = 0; y < h; y++)
		for (uint x = 0; x < w; x++)
		{
			const uint pos = ((y / 8) * (tw / 8) + (x / 8)) * 64 + morton8 (x & 7, y & 7);
			u8 *d = rgba + 4 * (y * w + x);
			if (nibble_fmt)
			{
				const u8 byte = src[pos >> 1];
				const u8 nib = (pos & 1) ? (byte >> 4) : (byte & 0xF);
				const u8 v = (u8)(nib * 17);
				if (fmt == 10)
				{
					d[0] = d[1] = d[2] = v;
					d[3] = 255;
				}
				else
				{
					d[0] = d[1] = d[2] = 255;
					d[3] = v;
				}
				continue;
			}

			const u8 *p = src + pos * bpp;
			if (fmt == 0) // RGBA8888
			{
				d[0] = p[0];
				d[1] = p[1];
				d[2] = p[2];
				d[3] = p[3];
			}
			else if (fmt == 1) // RGB888
			{
				d[0] = p[0];
				d[1] = p[1];
				d[2] = p[2];
				d[3] = 255;
			}
			else if (fmt == 2) // RGBA5551
			{
				const u16 c = rd_le16 (p);
				d[0] = expand5 (c >> 11);
				d[1] = expand5 (c >> 6);
				d[2] = expand5 (c >> 1);
				d[3] = (c & 1) ? 255 : 0;
			}
			else if (fmt == 3) // RGB565
			{
				const u16 c = rd_le16 (p);
				d[0] = expand5 (c >> 11);
				d[1] = (u8)(((c >> 5) & 63) * 255 / 63);
				d[2] = expand5 (c);
				d[3] = 255;
			}
			else if (fmt == 4) // RGBA4444
			{
				const u16 c = rd_le16 (p);
				d[0] = (u8)(((c >> 12) & 15) * 17);
				d[1] = (u8)(((c >> 8) & 15) * 17);
				d[2] = (u8)(((c >> 4) & 15) * 17);
				d[3] = (u8)((c & 15) * 17);
			}
			else if (fmt == 5) // LA88
			{
				d[0] = d[1] = d[2] = p[0];
				d[3] = p[1];
			}
			else if (fmt == 6) // HILO8
			{
				d[0] = p[0];
				d[1] = p[1];
				d[2] = 0;
				d[3] = 255;
			}
			else if (fmt == 7) // L8
			{
				d[0] = d[1] = d[2] = p[0];
				d[3] = 255;
			}
			else if (fmt == 8) // A8
			{
				d[0] = d[1] = d[2] = 255;
				d[3] = p[0];
			}
			else if (fmt == 9) // LA44
			{
				d[0] = d[1] = d[2] = (u8)((p[0] & 0xF) * 17);
				d[3] = (u8)((p[0] >> 4) * 17);
			}
		}

	*dest = rgba;
	*width = w;
	*height = h;
	return ERR_OK;
}

enumError DecodeCTPKTexture_RGBA (
	u8 **dest, uint *width, uint *height, const nintendo_ctpk_entry_t *entry)
{
	if (!entry)
		return EINVAL;
	return DecodePicaTexture (dest, width, height, entry->data, entry->width, entry->height,
		entry->format, entry->data_size);
}

enumError EncodeCTPK (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, ccp name)
{
	if (!dest || !dest_size || !rgba || !width || !height || width > 16384 || height > 16384)
		return EINVAL;

	const uint tw = (width + 7) & ~7u;
	const uint th = (height + 7) & ~7u;
	const u64 pixels = (u64)tw * th;
	if (pixels > (NFMT_MAX_OUTPUT - 0x100) / 4)
		return EFBIG;

	const uint image_size = 4 * pixels;
	u8 *tex_data = CALLOC (1, image_size);
	if (!tex_data)
		return ERR_CANT_CREATE;

	for (uint y = 0; y < height; y++)
	{
		for (uint x = 0; x < width; x++)
		{
			const uint pos = ((y / 8) * (tw / 8) + (x / 8)) * 64 + morton8 (x & 7, y & 7);
			const u8 *s = rgba + 4 * (y * width + x);
			u8 *d = tex_data + 4 * pos;
			d[0] = s[0];
			d[1] = s[1];
			d[2] = s[2];
			d[3] = s[3]; // RGBA8888
		}
	}

	ccp base_name = name ? strrchr (name, '/') : 0;
	base_name = base_name ? base_name + 1 : (name ? name : "tex_0.png");
	const size_t name_len = strlen (base_name) + 1;
	const uint name_area_size = (name_len + 3) & ~3u;

	const uint header_size = 0x20;
	const uint entry_size = 0x20;
	const uint texture_offset = (header_size + entry_size + name_area_size + 0x7F) & ~0x7Fu;
	const uint total_size = texture_offset + image_size;

	u8 *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE (tex_data);
		return ERR_CANT_CREATE;
	}

	memcpy (out, "CTPK", 4);
	wr_le16 (out + 4, 1);
	wr_le16 (out + 6, 1);
	wr_le32 (out + 8, texture_offset);
	wr_le32 (out + 12, image_size);

	u8 *e = out + 0x20;
	wr_le32 (e + 0, 0x40);
	wr_le32 (e + 4, image_size);
	wr_le32 (e + 8, 0);
	wr_le32 (e + 12, 0);
	wr_le16 (e + 16, (u16)width);
	wr_le16 (e + 18, (u16)height);
	e[20] = 1;
	e[21] = 0;

	memcpy (out + 0x40, base_name, strlen (base_name) + 1);
	memcpy (out + texture_offset, tex_data, image_size);
	FREE (tex_data);

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}

enumError CreateCTPK (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xFFFF)
		return EINVAL;

	uint names_size = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = entries[i].name ? entries[i].name : "tex";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;
		names_size += (uint)strlen (name) + 1;
	}
	const uint header_size = 0x20;
	const uint entries_size = 0x20 * n_entries;
	const uint string_table_size = (names_size + 3) & ~3u;
	const uint texture_offset = (header_size + entries_size + string_table_size + 0x7F) & ~0x7Fu;

	uint total_tex_size = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		total_tex_size += entries[i].size;
		total_tex_size = (total_tex_size + 0x7F) & ~0x7Fu;
	}

	const uint total_size = texture_offset + total_tex_size;
	u8 *out = CALLOC (1, total_size);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, "CTPK", 4);
	wr_le16 (out + 4, 1);
	wr_le16 (out + 6, (u16)n_entries);
	wr_le32 (out + 8, texture_offset);
	wr_le32 (out + 12, total_tex_size);

	uint str_off = header_size + entries_size;
	uint data_off = 0;

	for (uint i = 0; i < n_entries; i++)
	{
		u8 *e = out + header_size + i * 0x20;
		ccp name = entries[i].name ? entries[i].name : "tex";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;
		size_t nlen = strlen (name);

		wr_le32 (e + 0, str_off);
		wr_le32 (e + 4, entries[i].size);
		wr_le32 (e + 8, data_off);
		wr_le32 (e + 12, 0); // format = RGBA8
		wr_le16 (e + 16, 64);
		wr_le16 (e + 18, 64);
		e[20] = 1;
		e[21] = 0;

		memcpy (out + str_off, name, nlen + 1);
		str_off += (uint)nlen + 1;

		if (entries[i].size > 0 && entries[i].data)
			memcpy (out + texture_offset + data_off, entries[i].data, entries[i].size);

		data_off += entries[i].size;
		data_off = (data_off + 0x7F) & ~0x7Fu;
	}

	*dest = out;
	*dest_size = total_size;
	return ERR_OK;
}

