// Four small 3DS texture wrapper formats -- BTGA (aka LGA), STEX, DMPBM, and
// the texture chunk embedded in a CMB model's own "tex " section. All four
// wrap the same PICA200 8x8 Morton-tiled GPU format documented by
// xdanieldzd/Scarlet, just with a different tiny header, so their container
// parsing lives together here and hands pixels to lib-ctpk.c's common PICA
// decoder.

#include "lib-std.h"
#include "lib-image.h"
#include "lib-image-internal.h"
#include "lib-ctpk.h"

static uint Pica3DSFormat (u32 type, u32 format)
{
	if (format == 0x675a)
		return 12; // ETC1
	if (format == 0x675b)
		return 13; // ETC1+A4
	if (format == 0x6752) // RGBA
		return type == 0x8033 ? 4 : type == 0x8034 ? 2 : 0;
	if (format == 0x6754) // RGB
		return type == 0x8363 ? 3 : 1;
	if (format == 0x6756) // alpha
		return type == 0x6761 ? 11 : 8;
	if (format == 0x6757) // luminance
		return type == 0x6761 ? 10 : 7;
	if (format == 0x6758) // luminance + alpha
		return type == 0x6760 ? 9 : 5;
	return UINT_MAX;
}

static void Pica3DSFlipRGBA (u8 *rgba, uint width, uint height)
{
	for (uint y = 0; y < height / 2; y++)
	{
		u8 *top = rgba + (size_t)y * width * 4;
		u8 *bottom = rgba + (size_t)(height - 1 - y) * width * 4;
		for (uint x = 0; x < width * 4; x++)
		{
			const u8 temp = top[x];
			top[x] = bottom[x];
			bottom[x] = temp;
		}
	}
}

bool Pica3DSFilenameExt (ccp fname, ccp ext)
{
	if (!fname || !ext)
		return false;
	const char *dot = strrchr (fname, '.');
	return dot && !strcasecmp (dot, ext);
}

enumError DecodeBTGA_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, uint size)
{
	if (!dest || !width || !height || size < 0x38)
		return EINVAL;
	const u32 kind = rd_le32 (data);
	const uint w = kind == 1 ? rd_le16 (data + 0x0c) : kind == 0x400 ? rd_le16 (data + 0x18) : 0;
	const uint h = kind == 1 ? rd_le16 (data + 0x0e) : kind == 0x400 ? rd_le16 (data + 0x1a) : 0;
	const uint fmt = kind == 1 ? rd_le16 (data + 0x14) : kind == 0x400 ? rd_le16 (data + 0x20) : UINT_MAX;
	if (!w || !h || fmt > 13)
		return EINVAL;
	uint pica_fmt = fmt;
	u8 *rgba = 0;
	enumError err = DecodePicaTexture (&rgba, width, height, data + 0x38, w, h, pica_fmt, size - 0x38);
	if (!err)
		Pica3DSFlipRGBA (rgba, *width, *height);
	*dest = rgba;
	return err;
}

enumError DecodeSTEX_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, uint size)
{
	if (!dest || !width || !height || size < 0x20 || memcmp (data, "STEX", 4))
		return EINVAL;
	const uint w = rd_le32 (data + 0x0c), h = rd_le32 (data + 0x10);
	const uint pica_fmt = Pica3DSFormat (rd_le32 (data + 0x14), rd_le32 (data + 0x18));
	const uint image_size = rd_le32 (data + 0x1c);
	const uint image_off = rd_le32 (data + 0x20) == 0x80 ? 0x80 : 0x20;
	if (!w || !h || pica_fmt == UINT_MAX || image_off >= size)
		return EINVAL;
	const uint available = size - image_off;
	return DecodePicaTexture (dest, width, height, data + image_off, w, h, pica_fmt,
		image_size && image_size <= available ? image_size : available);
}

enumError DecodeDMPBM_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, uint size)
{
	if (!dest || !width || !height || size < 14 || memcmp (data, "DMPBM", 5))
		return EINVAL;
	const uint fmt = data[5], w = rd_le32 (data + 6), h = rd_le32 (data + 10);
	if (!w || !h || w > 16384 || h > 16384 || fmt > 4)
		return EINVAL;
	const uint pixel_off = fmt == 4 ? 14 + 512 : 14;
	if (pixel_off > size)
		return EINVAL;
	if (fmt != 4)
	{
		static const uint pica[] = { 8, 2, 4, 0 };
		enumError err = DecodePicaTexture (dest, width, height, data + pixel_off, w, h, pica[fmt],
			size - pixel_off);
		if (!err)
			Pica3DSFlipRGBA (*dest, *width, *height);
		return err;
	}

	const uint tw = EXPAND8 (w), th = EXPAND8 (h);
	if ((u64)tw * th > size - pixel_off)
		return EINVAL;
	u8 *rgba = MALLOC ((size_t)w * h * 4);
	if (!rgba)
		return ERR_CANT_CREATE;
	for (uint y = 0; y < h; y++)
		for (uint x = 0; x < w; x++)
		{
			const uint pos = ((y / 8) * (tw / 8) + x / 8) * 64 + morton8 (x & 7, y & 7);
			const u16 c = rd_le16 (data + 14 + 2 * data[pixel_off + pos]); // A1B5G5R5
			u8 *d = rgba + 4 * ((size_t)y * w + x);
			d[0] = expand5 (c);
			d[1] = expand5 (c >> 5);
			d[2] = expand5 (c >> 10);
			d[3] = c & 0x8000 ? 255 : 0;
		}
	Pica3DSFlipRGBA (rgba, w, h);
	*dest = rgba;
	*width = w;
	*height = h;
	return ERR_OK;
}

enumError DecodeCMBTexture_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, uint size)
{
	if (!dest || !width || !height || size < 0x38 || memcmp (data, "cmb ", 4))
		return EINVAL;
	const u32 revision = rd_le32 (data + 8);
	const uint chunks = revision == 6 ? 6 : revision == 10 || revision == 12 || revision == 15 ? 7 : 0;
	const uint tex_index = revision == 6 ? 2 : 3;
	const uint raw_count = revision == 6 ? 2 : chunks ? 3 : 0;
	if (!chunks || 0x24 + 4 * (chunks + raw_count) > size)
		return EINVAL;
	const uint tex_chunk = rd_le32 (data + 0x24 + 4 * tex_index);
	const uint tex_data = rd_le32 (data + 0x24 + 4 * chunks + 4); // raw-data slot 1
	if (!tex_data || tex_chunk > size || tex_data > size || tex_chunk + 0x30 > size
		|| memcmp (data + tex_chunk, "tex ", 4))
		return EINVAL;
	const uint count = rd_le32 (data + tex_chunk + 8);
	if (!count || tex_chunk + 12 + 36 > size)
		return EINVAL;
	const u8 *entry = data + tex_chunk + 12; // first texture entry only
	const uint bytes = rd_le32 (entry), w = rd_le16 (entry + 8), h = rd_le16 (entry + 10);
	const uint pica_fmt = Pica3DSFormat (rd_le16 (entry + 14), rd_le16 (entry + 12));
	const uint rel = rd_le32 (entry + 16);
	if (!w || !h || pica_fmt == UINT_MAX || rel > size - tex_data || bytes > size - tex_data - rel)
		return EINVAL;
	return DecodePicaTexture (dest, width, height, data + tex_data + rel, w, h, pica_fmt, bytes);
}


static enumError CreatePica3DSRGBA8 ( Image_t *img, bool flip_y,
		u8 **dest, uint *dest_size )
{
	*dest = 0;
	*dest_size = 0;
	if ( img->iform != IMG_X_RGB && ConvertToRGB (img, img, PAL_AUTO) )
		return ERR_INVALID_DATA;
	if (!img->width || !img->height || img->width > 0xffff || img->height > 0xffff)
		return ERROR0 (ERR_INVALID_DATA, "Invalid PICA texture dimensions: %ux%u\n", img->width, img->height);

	const uint tw = (img->width + 7) & ~7u;
	const uint th = (img->height + 7) & ~7u;
	const uint size = tw * th * 4;
	u8 *raw = CALLOC (1, size);
	if (!raw)
		return ERR_CANT_CREATE;

	for (uint y = 0; y < img->height; y++)
		for (uint x = 0; x < img->width; x++)
		{
			const uint py = flip_y ? img->height - 1 - y : y;
			const uint pos = ((y / 8) * (tw / 8) + x / 8) * 64 + morton8 (x & 7, y & 7);
			memcpy (raw + 4 * pos, img->data + (py * img->xwidth + x) * 4, 4);
		}

	*dest = raw;
	*dest_size = size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------

enumError SavePica3DSTexture (Image_t *img, file_format_t fform,
		FILE *fo, ccp path, bool overwrite)
{
	const bool flip_y = fform == FF_BTGA || fform == FF_DMPBM;
	u8 *raw;
	uint raw_size;
	enumError err = CreatePica3DSRGBA8 (img, flip_y, &raw, &raw_size);
	if (err)
		return err;

	uint head_size;
	switch (fform)
	{
		case FF_BTGA: head_size = 0x38; break;
		case FF_DMPBM: head_size = 14; break;
		case FF_STEX: head_size = 0x80; break;
		case FF_CMB: head_size = 0x80; break;
		default: FREE(raw); return ERR_INVALID_DATA;
	}
	const uint total_size = head_size + raw_size;
	u8 *out = CALLOC (1, total_size);
	if (!out)
	{
		FREE(raw);
		return ERR_CANT_CREATE;
	}

	if (fform == FF_BTGA)
	{
		wr_le32 (out, 1); wr_le32 (out+4, 0x20); wr_le32 (out+8, 0x20);
		wr_le16 (out+12, img->width); wr_le16 (out+14, img->height);
		wr_le16 (out+20, 0); wr_le32 (out+24, 1);
	}
	else if (fform == FF_DMPBM)
	{
		memcpy (out, "DMPBM", 5); out[5] = 3;
		wr_le32 (out+6, img->width); wr_le32 (out+10, img->height);
	}
	else if (fform == FF_STEX)
	{
		memcpy (out, "STEX", 4); wr_le32 (out+12, img->width); wr_le32 (out+16, img->height);
		wr_le32 (out+20, 0x1401); wr_le32 (out+24, 0x6752); wr_le32 (out+28, raw_size);
		wr_le32 (out+32, head_size);
	}
	else
	{
		memcpy (out, "cmb ", 4); wr_le32 (out+4, total_size); wr_le32 (out+8, 6);
		wr_le32 (out+0x24+2*4, 0x50); wr_le32 (out+0x24+6*4+4, head_size);
		memcpy (out+0x50, "tex ", 4); wr_le32 (out+0x54, 36); wr_le32 (out+0x58, 1);
		u8 *te = out + 0x5c;
		wr_le32 (te, raw_size); wr_le16 (te+8, img->width); wr_le16 (te+10, img->height);
		wr_le16 (te+12, 0x6752); wr_le16 (te+14, 0x1401);
		ccp base = FindFilename (path, 0); size_t n = base ? strcspn (base,".") : 0;
		if (n > 15) n = 15; if (n) memcpy (te+20, base, n);
	}
	memcpy (out + head_size, raw, raw_size);
	FREE(raw);
	err = SaveImageBuffer (img, fo, path, overwrite, out, total_size, GetNameFF(0,fform));
	FREE(out);
	return err;
}

//-----------------------------------------------------------------------------

