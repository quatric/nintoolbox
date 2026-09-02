#include "lib-nut.h"
#include "lib-std.h"

bool IsNUT (const u8 *data, size_t size)
{
	if (!data || size < 16)
		return false;
	return !memcmp (data, "NTP3", 4) || !memcmp (data, "3PTN", 4)
		|| !memcmp (data, "NTWU", 4) || !memcmp (data, "UWTM", 4)
		|| !memcmp (data, "NUT\0", 4) || !memcmp (data, "\0TUN", 4);
}

enumError ScanNUT (nut_t *nut, const u8 *data, size_t size)
{
	if (!nut || !data || size < 16 || !IsNUT (data, size))
		return ERR_INVALID_DATA;

	memset (nut, 0, sizeof (*nut));
	nut->data = data;
	nut->size = size;

	u16 v_be = be16 (data + 4);
	u16 c_be = be16 (data + 6);
	u16 v_le = le16 (data + 4);

	if (c_be > 0 && c_be <= 4096 && (v_be == 0x0200 || v_be == 0x0100 || v_be == 0x0002 || v_be == 0x0001))
		nut->is_big_endian = (v_be == 0x0200 || v_be == 0x0100);
	else
		nut->is_big_endian = (v_le == 0x0002 || v_le == 0x0001);

#define NT16(p) (nut->is_big_endian ? be16 (p) : le16 (p))
#define NT32(p) (nut->is_big_endian ? be32 (p) : le32 (p))

	nut->version = NT16 (data + 4);
	nut->n_textures = NT16 (data + 6);
	nut->count = nut->n_textures;

	if (nut->n_textures == 0 || nut->n_textures > 4096)
		return ERR_INVALID_DATA;

	nut->textures = CALLOC (nut->n_textures, sizeof (nut_texture_t));
	if (!nut->textures)
		return ERR_OUT_OF_MEMORY;

	const u8 *p = data + 16;
	const u8 *end = data + size;

	for (uint i = 0; i < nut->n_textures; i++)
	{
		if (p + 32 > end)
			break;

		nut_texture_t *t = &nut->textures[i];
		t->total_size = NT32 (p);
		t->palette_size = NT32 (p + 4);
		t->data_size = NT32 (p + 8);
		t->header_size = NT16 (p + 12);
		t->name_len = NT16 (p + 14);

		if (p + 24 <= end)
		{
			t->width = NT16 (p + 16);
			t->height = NT16 (p + 18);
			t->num_mips = NT32 (p + 20);
			t->pixel_format = NT32 (p + 24);
			t->data_offset = NT32 (p + 28);
		}

		snprintf (t->name, sizeof (t->name), "texture_%03u", i);

		// Resolve data pointer
		if (t->data_offset > 0 && t->data_offset + t->data_size <= size)
			t->data = data + t->data_offset;
		else if ((size_t)(p - data) + t->header_size + t->data_size <= size)
			t->data = p + t->header_size;
		else if (p + 32 + t->data_size <= end)
			t->data = p + 32;

		size_t adv = t->header_size >= 32 ? t->header_size : 48;
		if (t->data_offset == (u32)(p - data + adv))
			adv += t->data_size;
		else if (t->total_size > adv && (size_t)(p - data) + t->total_size <= size && t->data_offset == 0)
			adv = t->total_size;

		p += adv;
	}

	return ERR_OK;
}

int ParseNUT (nut_t *nut, const uint8_t *data, size_t size)
{
	return ScanNUT (nut, data, size) == ERR_OK ? 1 : 0;
}

void ResetNUT (nut_t *nut)
{
	if (!nut)
		return;
	if (nut->textures)
		FREE (nut->textures);
	memset (nut, 0, sizeof (*nut));
}

void FreeNUT (nut_t *nut)
{
	ResetNUT (nut);
}

static inline void wr_le16 (u8 *p, u16 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

static inline void wr_le32 (u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

enumError ExtractNUTTexture (const nut_t *nut, uint index, u8 **dest, size_t *dest_size, char *ext_out, size_t ext_max)
{
	if (!nut || !dest || !dest_size || index >= nut->n_textures)
		return ERR_INVALID_DATA;

	const nut_texture_t *t = &nut->textures[index];
	if (!t->data || t->data_size == 0)
		return ERR_INVALID_DATA;

	// Check if we can wrap into DDS for standard DXT1 / DXT3 / DXT5 formats
	bool is_dxt1 = (t->pixel_format == 0x0000 || t->pixel_format == 0x01);
	bool is_dxt3 = (t->pixel_format == 0x0001 || t->pixel_format == 0x02);
	bool is_dxt5 = (t->pixel_format == 0x0002 || t->pixel_format == 0x03);

	if (is_dxt1 || is_dxt3 || is_dxt5)
	{
		size_t dds_total = 128 + t->data_size;
		u8 *dds = CALLOC (1, dds_total);
		if (!dds)
			return ERR_OUT_OF_MEMORY;

		memcpy (dds, "DDS ", 4);
		wr_le32 (dds + 4, 124); // header size
		wr_le32 (dds + 8, 0x00081007); // DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT
		wr_le32 (dds + 12, t->height);
		wr_le32 (dds + 16, t->width);
		wr_le32 (dds + 20, t->data_size); // linear size
		wr_le32 (dds + 24, 0); // depth
		wr_le32 (dds + 28, t->num_mips > 0 ? t->num_mips : 1);

		// PixelFormat (32 bytes at offset 76)
		wr_le32 (dds + 76, 32); // size
		wr_le32 (dds + 80, 0x04); // DDPF_FOURCC
		if (is_dxt1)
			memcpy (dds + 84, "DXT1", 4);
		else if (is_dxt3)
			memcpy (dds + 84, "DXT3", 4);
		else
			memcpy (dds + 84, "DXT5", 4);

		// Caps (offset 108)
		wr_le32 (dds + 108, 0x1000); // DDSCAPS_TEXTURE

		memcpy (dds + 128, t->data, t->data_size);
		*dest = dds;
		*dest_size = dds_total;
		if (ext_out && ext_max > 0)
			snprintf (ext_out, ext_max, ".dds");
		return ERR_OK;
	}

	// Raw payload export
	u8 *out = MALLOC (t->data_size + 1);
	if (!out)
		return ERR_OUT_OF_MEMORY;
	memcpy (out, t->data, t->data_size);
	out[t->data_size] = 0;
	*dest = out;
	*dest_size = t->data_size;

	if (ext_out && ext_max > 0)
	{
		if (t->pixel_format == 0x0014)
			snprintf (ext_out, ext_max, ".rgba");
		else
			snprintf (ext_out, ext_max, ".bin");
	}
	return ERR_OK;
}

// Decode BC1/DXT1 block to RGBA
static void decode_bc1_block (const uint8_t *src, uint8_t *dst, uint32_t stride)
{
	uint16_t c0 = (uint16_t)src[0] | ((uint16_t)src[1] << 8);
	uint16_t c1 = (uint16_t)src[2] | ((uint16_t)src[3] << 8);
	uint8_t rgb[4][4];

	rgb[0][0] = (uint8_t)(((c0 >> 11) & 0x1F) * 255 / 31);
	rgb[0][1] = (uint8_t)(((c0 >> 5) & 0x3F) * 255 / 63);
	rgb[0][2] = (uint8_t)((c0 & 0x1F) * 255 / 31);
	rgb[0][3] = 255;

	rgb[1][0] = (uint8_t)(((c1 >> 11) & 0x1F) * 255 / 31);
	rgb[1][1] = (uint8_t)(((c1 >> 5) & 0x3F) * 255 / 63);
	rgb[1][2] = (uint8_t)((c1 & 0x1F) * 255 / 31);
	rgb[1][3] = 255;

	if (c0 > c1)
	{
		rgb[2][0] = (uint8_t)((2 * rgb[0][0] + rgb[1][0]) / 3);
		rgb[2][1] = (uint8_t)((2 * rgb[0][1] + rgb[1][1]) / 3);
		rgb[2][2] = (uint8_t)((2 * rgb[0][2] + rgb[1][2]) / 3);
		rgb[2][3] = 255;

		rgb[3][0] = (uint8_t)((rgb[0][0] + 2 * rgb[1][0]) / 3);
		rgb[3][1] = (uint8_t)((rgb[0][1] + 2 * rgb[1][1]) / 3);
		rgb[3][2] = (uint8_t)((rgb[0][2] + 2 * rgb[1][2]) / 3);
		rgb[3][3] = 255;
	}
	else
	{
		rgb[2][0] = (uint8_t)((rgb[0][0] + rgb[1][0]) / 2);
		rgb[2][1] = (uint8_t)((rgb[0][1] + rgb[1][1]) / 2);
		rgb[2][2] = (uint8_t)((rgb[0][2] + rgb[1][2]) / 2);
		rgb[2][3] = 255;

		rgb[3][0] = rgb[3][1] = rgb[3][2] = 0;
		rgb[3][3] = 0;
	}

	for (int y = 0; y < 4; y++)
	{
		uint8_t bits = src[4 + y];
		for (int x = 0; x < 4; x++)
		{
			uint8_t idx = (bits >> (x * 2)) & 3;
			memcpy (dst + y * stride + x * 4, rgb[idx], 4);
		}
	}
}

int DecodeNUTTextureToRGBA (const nut_texture_t *tex, uint8_t **out_rgba, uint32_t *out_w, uint32_t *out_h)
{
	if (!tex || !tex->data || !tex->width || !tex->height || !out_rgba)
		return 0;

	uint32_t w = tex->width;
	uint32_t h = tex->height;
	uint8_t *rgba = MALLOC ((size_t)w * h * 4);
	if (!rgba)
		return 0;

	if (tex->pixel_format == 0) // DXT1 / BC1
	{
		uint32_t bw = (w + 3) / 4;
		uint32_t bh = (h + 3) / 4;
		const uint8_t *src = tex->data;
		for (uint32_t by = 0; by < bh; by++)
		{
			for (uint32_t bx = 0; bx < bw; bx++)
			{
				uint8_t blk[4 * 4 * 4];
				decode_bc1_block (src, blk, 16);
				src += 8;
				for (uint32_t py = 0; py < 4 && by * 4 + py < h; py++)
				{
					for (uint32_t px = 0; px < 4 && bx * 4 + px < w; px++)
					{
						size_t dp = ((by * 4 + py) * w + (bx * 4 + px)) * 4;
						size_t sp = (py * 4 + px) * 4;
						memcpy (rgba + dp, blk + sp, 4);
					}
				}
			}
		}
	}
	else if (tex->pixel_format == 8 || tex->pixel_format == 14 || tex->pixel_format == 0x0014) // RGBA8
	{
		memcpy (rgba, tex->data, (size_t)w * h * 4);
	}
	else
	{
		// Fallback: clear to white
		memset (rgba, 255, (size_t)w * h * 4);
	}

	*out_rgba = rgba;
	if (out_w) *out_w = w;
	if (out_h) *out_h = h;
	return 1;
}

enumError CreateNUT (u8 **dest, size_t *dest_size, uint n_textures,
	const u16 *widths, const u16 *heights, const u32 *formats, const u8 *const *tex_data, const size_t *tex_sizes)
{
	if (!dest || !dest_size || !n_textures)
		return ERR_INVALID_DATA;

	size_t header_size = 16;
	size_t per_tex_hdr = 48;
	size_t total_data_size = 0;

	for (uint i = 0; i < n_textures; i++)
		total_data_size += (tex_sizes ? tex_sizes[i] : 0);

	size_t total_size = header_size + n_textures * per_tex_hdr + total_data_size;
	u8 *buf = CALLOC (1, total_size);
	if (!buf)
		return ERR_OUT_OF_MEMORY;

	// Header
	memcpy (buf, "NTP3", 4);
	wr_le16 (buf + 4, 0x0200); // version
	wr_le16 (buf + 6, n_textures);

	u8 *th_ptr = buf + header_size;
	u8 *data_ptr = buf + header_size + n_textures * per_tex_hdr;

	for (uint i = 0; i < n_textures; i++)
	{
		size_t tsz = tex_sizes ? tex_sizes[i] : 0;
		u32 data_off = (u32)(data_ptr - buf);

		wr_le32 (th_ptr + 0, (u32)(per_tex_hdr + tsz)); // total_size
		wr_le32 (th_ptr + 4, 0); // palette_size
		wr_le32 (th_ptr + 8, (u32)tsz); // data_size
		wr_le16 (th_ptr + 12, (u16)per_tex_hdr); // header_size
		wr_le16 (th_ptr + 14, 0); // name_len
		wr_le16 (th_ptr + 16, widths ? widths[i] : 64);
		wr_le16 (th_ptr + 18, heights ? heights[i] : 64);
		wr_le32 (th_ptr + 20, 1); // num_mips
		wr_le32 (th_ptr + 24, formats ? formats[i] : 0x0000); // DXT1 default
		wr_le32 (th_ptr + 28, data_off);

		if (tsz > 0 && tex_data && tex_data[i])
			memcpy (data_ptr, tex_data[i], tsz);

		th_ptr += per_tex_hdr;
		data_ptr += tsz;
	}

	*dest = buf;
	*dest_size = total_size;
	return ERR_OK;
}
