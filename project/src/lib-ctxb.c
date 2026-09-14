// Grezzo 3DS CTXB texture container -- Image_t save glue.

#include "lib-std.h"
#include "lib-image.h"
#include "lib-image-internal.h"
#include "lib-nintendo.h"

enumError SaveCTXB (Image_t *img, FILE *fo, ccp path, bool overwrite)
{
	DASSERT (img);
	DASSERT (path);

	enumError err = ERR_OK;
	if (img->iform != IMG_X_RGB)
	{
		err = ConvertToRGB (img, img, PAL_AUTO);
		if (err)
			return err;
	}

	const uint width = img->width;
	const uint height = img->height;
	const uint tw = (width + 7) & ~7u;
	const uint th = (height + 7) & ~7u;
	const uint img_data_size = tw * th * 4;

	u8 *img_data = CALLOC (1, img_data_size);
	if (!img_data)
		return ERR_CANT_CREATE;

	const u8 *src = img->data;
	for (uint y = 0; y < height; y++)
	{
		for (uint x = 0; x < width; x++)
		{
			const uint pos = ((y / 8) * (tw / 8) + (x / 8)) * 64 + morton8 (x & 7, y & 7);
			const u8 *s = src + (y * img->xwidth + x) * 4;
			u8 *d = img_data + 4 * pos;
			d[0] = s[0];
			d[1] = s[1];
			d[2] = s[2];
			d[3] = s[3];
		}
	}

	const uint hdr_size = 24;
	const uint chunk_size = 12 + 36;
	const uint tex_data_offset = hdr_size + chunk_size;
	const uint total_sz = tex_data_offset + img_data_size;

	u8 *ctxb = CALLOC (1, total_sz);
	if (!ctxb)
	{
		FREE (img_data);
		return ERR_CANT_CREATE;
	}

	memcpy (ctxb, "ctxb", 4);
	wr_le32 (ctxb + 4, total_sz);
	wr_le32 (ctxb + 8, 1); // chunk count
	wr_le32 (ctxb + 12, 0);
	wr_le32 (ctxb + 16, hdr_size); // chunk offset
	wr_le32 (ctxb + 20, tex_data_offset); // tex data offset

	u8 *chunk = ctxb + hdr_size;
	memcpy (chunk, "tex ", 4);
	wr_le32 (chunk + 4, 36); // sec size
	wr_le32 (chunk + 8, 1); // tex count

	u8 *tentry = chunk + 12;
	wr_le32 (tentry + 0, img_data_size);
	wr_le16 (tentry + 4, 0);
	wr_le16 (tentry + 6, 0);
	wr_le16 (tentry + 8, (u16)width);
	wr_le16 (tentry + 10, (u16)height);
	wr_le32 (tentry + 12, 0x14016752); // RGBA8
	wr_le32 (tentry + 16, 0); // data rel offset

	ccp fname = FindFilename (path, 0);
	if (!fname)
		fname = "texture";
	char tname[16];
	memset (tname, 0, sizeof (tname));
	char *dot = strchr (fname, '.');
	size_t flen = dot ? (size_t)(dot - fname) : strlen (fname);
	if (flen > 15)
		flen = 15;
	memcpy (tname, fname, flen);
	memcpy (tentry + 20, tname, 16);

	memcpy (ctxb + tex_data_offset, img_data, img_data_size);
	FREE (img_data);

	File_t f;
	if (fo)
	{
		InitializeFile (&f);
		f.f = fo;
		f.is_writing = true;
	}
	else
	{
		err = CreateFileOpt (&f, true, path, testmode, overwrite ? path : 0);
		if (err || !f.f)
		{
			ResetFile (&f, 0);
			FREE (ctxb);
			return err;
		}
	}

	size_t stat = fwrite (ctxb, 1, total_sz, f.f);
	FREE (ctxb);

	if (stat != total_sz)
	{
		err = ERROR0 (ERR_WRITE_FAILED, "Error while writing CTXB data: %s\n", path);
		RegisterFileError (&f, ERR_WRITE_FAILED);
	}

	if (opt_preserve)
		memcpy (&f.fatt, &img->fatt, sizeof (f.fatt));

	if (fo)
		f.f = 0;
	err = ResetFile (&f, opt_preserve);

	return err;
}

//-----------------------------------------------------------------------------

