// ActImagine AJPG still-image codec (GBA / Wii Message Board) --
// Image_t save glue for the ajpg/ codec core.

#include "lib-std.h"
#include "lib-image.h"
#include "lib-ajpg.h"
#include "ajpg/ajpg.h"

//-----------------------------------------------------------------------------

enumError SaveAJPG (Image_t *img, // valid image
	FILE *fo, // output file, if NULL then use path1+path2
	ccp path1, // NULL or part #1 of path
	ccp path2, // NULL or part #2 of path
	bool overwrite // true: force overwriting
)
{
	DASSERT (img);

	if (!path2 || !*path2)
	{
		path2 = path1;
		path1 = 0;
	}

	char pathbuf[PATH_MAX];
	ccp path = PathCatPP (pathbuf, sizeof (pathbuf), path1, path2);
	PRINT ("SaveAJPG() %s\n", path);

	Transform2XIMG (img);
	enumError err = ExecTransformIMG (img);
	if (err)
		return err;

	if (img->iform != IMG_X_RGB)
	{
		err = ConvertToRGB (img, img, PAL_AUTO);
		if (err)
			return err;
	}

	u8 *rgba_data = 0;
	bool alloced = false;
	if (img->xwidth == img->width && img->xheight == img->height)
	{
		rgba_data = img->data;
	}
	else
	{
		rgba_data = MALLOC (img->width * img->height * 4);
		alloced = true;
		u8 *dest = rgba_data;
		const u8 *src = img->data;
		for (uint y = 0; y < img->height; y++)
		{
			memcpy (dest, src, img->width * 4);
			dest += img->width * 4;
			src += img->xwidth * 4;
		}
	}

	uint8_t *out_data = 0;
	size_t out_size = 0;
	int quality = 80;
	if (!AjpgEncodeRGBA (rgba_data, img->width, img->height, quality, &out_data, &out_size))
	{
		if (alloced)
			FREE (rgba_data);
		return ERROR0 (ERR_WRITE_FAILED,
			"AJPG encode failed (requires even dimensions and max 2047x2047): %s\n", path);
	}

	if (alloced)
		FREE (rgba_data);

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
			AjpgFree (out_data);
			return err;
		}
	}

	size_t stat = fwrite (out_data, 1, out_size, f.f);
	AjpgFree (out_data);

	if (stat != out_size)
	{
		err = ERROR0 (ERR_WRITE_FAILED, "Error while writing AJPG data: %s\n", path);
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
