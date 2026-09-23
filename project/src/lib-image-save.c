
/***************************************************************************
 *                         _______ _______ _______                         *
 *                        |  ___  |____   |  ___  |                        *
 *                        | |   |_|    / /| |   |_|                        *
 *                        | |_____    / / | |_____                         *
 *                        |_____  |  / /  |_____  |                        *
 *                         _    | | / /    _    | |                        *
 *                        | |___| |/ /____| |___| |                        *
 *                        |_______|_______|_______|                        *
 *                                                                         *
 *                            Wiimms SZS Tools                             *
 *                          https://szs.wiimm.de/                          *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This file is part of the SZS project.                                 *
 *   Visit https://szs.wiimm.de/ for project details and sources.          *
 *                                                                         *
 *   Copyright (c) 2011-2024 by Dirk Clemens <wiimm@wiimm.de>              *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   See file gpl-2.0.txt or http://www.gnu.org/licenses/gpl-2.0.txt       *
 *                                                                         *
 ***************************************************************************/

#include "lib-std.h"
#include "lib-image.h"
#include "lib-image-internal.h"
#include "lib-plt0.h"
#include "lib-smdh.h"
#include "lib-nitro.h"
#include "lib-nut.h"
#include "lib-ctxb.h"
#include "lib-ajpg.h"
#include "lib-pica3ds.h"
#include "lib-dds.h"
#include "lib-astc-file.h"

///////////////////////////////////////////////////////////////////////////////
///////////////			MipmapOptions_t			///////////////
///////////////////////////////////////////////////////////////////////////////

void SetupMipmapOptions (MipmapOptions_t *mmo)
{
	DASSERT (mmo);

	// memset(mmo,0,sizeof(*mmo));
	mmo->valid = true;
	mmo->is_tpl = false;

	if (opt_n_images)
	{
		mmo->force = true;
		mmo->n_mipmap = opt_n_images - 1;
		mmo->n_image = opt_n_images;
		mmo->min_size = 1;
	}
	else
	{
		mmo->force = false;
		mmo->n_mipmap = opt_max_images - 1;
		mmo->n_image = opt_max_images;
		mmo->min_size = opt_min_mipmap_size;
	}

	PRINT ("MM-OPT/SETUP: %s\n", TextMipmapOptions (mmo));
}

///////////////////////////////////////////////////////////////////////////////

void SetupMipmapOptionsTPL (MipmapOptions_t *mmo)
{
	DASSERT (mmo);

	// memset(mmo,0,sizeof(*mmo));
	mmo->valid = true;
	mmo->force = true;
	mmo->is_tpl = true;
	mmo->n_mipmap = 0;
	mmo->n_image = 1;
	mmo->min_size = 1;

	PRINT ("MM-OPT/SETUP1: %s\n", TextMipmapOptions (mmo));
}

///////////////////////////////////////////////////////////////////////////////

void CopyMipmapOptions (MipmapOptions_t *dest, const MipmapOptions_t *src)
{
	DASSERT (dest);
	if (src)
	{
		memcpy (dest, src, sizeof (*dest));
		PRINT ("MM-OPT/COPY: %s\n", TextMipmapOptions (dest));
	}
	else
		SetupMipmapOptions (dest);
}

///////////////////////////////////////////////////////////////////////////////

void MipmapOptionsByImage (MipmapOptions_t *mmo, const Image_t *img)
{
	DASSERT (mmo);
	if (!mmo->valid)
		SetupMipmapOptions (mmo);

	if (!mmo->force && img)
	{
		int n_image = img->mipmap ? CountMipmapsIMG (img) + 1 : mmo->n_image;
		if (n_image < img->info_n_image)
			n_image = img->info_n_image;

		if (n_image > MAX_MIPMAPS)
			n_image = MAX_MIPMAPS + 1;
		else if (n_image < 1)
			n_image = opt_max_images > 0 ? opt_max_images : 1;

		mmo->n_image = n_image;
		mmo->n_mipmap = n_image - 1;
	}

	PRINT ("MM-OPT/IMG: %s\n", TextMipmapOptions (mmo));
}

///////////////////////////////////////////////////////////////////////////////

ccp InfoMipmapOptions (const MipmapOptions_t *mmo)
{
	if (!mmo)
		return "--";

	if (!mmo->valid)
		return "!!";

	char buf[20];
	int len = snprintf (
		buf, sizeof (buf), "%s%d/%d", mmo->force ? "f" : "m", mmo->n_mipmap, mmo->min_size);

	char *res = GetCircBuf (++len);
	memcpy (res, buf, len);
	return res;
}

///////////////////////////////////////////////////////////////////////////////

ccp TextMipmapOptions (const MipmapOptions_t *mmo)
{
	if (!mmo)
		return "--";

	if (!mmo->valid)
		return "INVALID!";

	char buf[100];
	int len = snprintf (buf, sizeof (buf), "force=%d, nm=%d, ni=%d, minsize=%d", mmo->force,
		mmo->n_mipmap, mmo->n_image, mmo->min_size);

	char *res = GetCircBuf (++len);
	memcpy (res, buf, len);
	return res;
}

///////////////////////////////////////////////////////////////////////////////

void PrintMipmapOptions (FILE *f, int indent, const MipmapOptions_t *mmo)
{
	DASSERT (f);
	DASSERT (mmo);

	indent = NormalizeIndent (indent);
	fprintf (f, "%*sMM-OPT: %s\n", indent, "", TextMipmapOptions (mmo));
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			mipmap helpers			///////////////
///////////////////////////////////////////////////////////////////////////////
// mipmap_info_t is declared in lib-image-internal.h, shared with every
// per-format Save*() that calls PrepareImages()/WriteImageData() below.

///////////////////////////////////////////////////////////////////////////////

void ResetMMI (mipmap_info_t *mmi)
{
	DASSERT (mmi);
	ResetIMG (&mmi->img);
	memset (mmi, 0, sizeof (*mmi));
	SetupMipmapOptions (&mmi->mmo);
}

///////////////////////////////////////////////////////////////////////////////

enumError PrepareImages (mipmap_info_t *mmi, // valid mipmap info
	const Image_t *src_img, // pointer to source image
	const MipmapOptions_t *mmo // NULL or mipmap options

	// RETURNS (if return value == ERR_OK):
	//	 mmi->n_mipmap      : number of mipmaps to store
	//   mmi->image_size    : total image size (main+mipmaps)
	//	 mmi->img.info_size : data size of main image
)
{
	DASSERT (mmi);
	DASSERT (src_img);

	memset (mmi, 0, sizeof (*mmi));
	mmi->src_img = src_img;

	CopyMipmapOptions (&mmi->mmo, mmo);
	MipmapOptionsByImage (&mmi->mmo, src_img);
	PRINT ("PrepareImages() MMO: %s\n", TextMipmapOptions (&mmi->mmo));

	//--- copy & convert images

	CopyIMG (&mmi->img, true, src_img, false);
	const bool is_tpl = mmi->mmo.is_tpl;
	if (!is_tpl)
	{
		Transform2InternIMG (&mmi->img);
		enumError err = ExecTransformIMG (&mmi->img);
		if (err)
			return err;
	}

	//--- calculate image size

	const ImageGeometry_t *geo = GetImageGeometry (mmi->img.iform);
	if (!geo || geo->is_x)
		return ERROR0 (ERR_INTERNAL, 0);

	uint size = 0;
	uint wd = mmi->img.width;
	uint ht = mmi->img.height;
	uint n_image = mmi->mmo.n_image;

	for (int ni = 0; ni < n_image; ni++)
	{
		size += CalcImageSize (
			wd, ht, geo->bits_per_pixel, geo->block_width, geo->block_height, 0, 0, 0, 0);
		if (!ni)
			mmi->img.info_size = size;
		if (!is_tpl)
		{
			wd /= 2;
			ht /= 2;
			DASSERT (opt_min_mipmap_size >= 1);
			if (wd < mmi->mmo.min_size || ht < mmi->mmo.min_size)
			{
				n_image = ni + 1;
				break;
			}
		}
	}

	mmi->n_mipmap = n_image - 1;
	mmi->image_size = size;

	PRINT ("SETUP IMAGES: N=1+%u, size=%u=0x%x, m0=%s\n", n_image - 1, size, size,
		InfoMipmapOptions (&mmi->mmo));
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

enumError PrepareImagesTPL (mipmap_info_t *mmi, // valid mipmap info
	const Image_t *src_img // pointer to source image

	// RETURNS (if return value == ERR_OK):
	//	 mmi->n_mipmap      : number of mipmaps to store
	//   mmi->image_size    : total image size (main+mipmaps)
	//	 mmi->img.info_size : data size of main image
)
{
	DASSERT (mmi);
	DASSERT (src_img);

	MipmapOptions_t mmo;
	SetupMipmapOptionsTPL (&mmo);
	return PrepareImages (mmi, src_img, &mmo);
}

///////////////////////////////////////////////////////////////////////////////

enumError WriteImageData (mipmap_info_t *mmi, // valid mipmap info
	u8 *data, // destination buffer
	u8 **p_dest // not NULL: store next destination
)
{
	DASSERT (mmi);
	DASSERT (data);

	const Image_t *img = &mmi->img;
	memcpy (data, img->data, img->info_size);
	if (p_dest)
		*p_dest = data + img->info_size;
	if (!mmi->n_mipmap)
		return ERR_OK;

	const ImageGeometry_t *geo = GetImageGeometry (img->iform);
	if (!geo || geo->is_x)
		return ERROR0 (ERR_INTERNAL, 0);

	Image_t temp;
	InitializeIMG (&temp);

	enumError err = ERR_OK;
	u8 *dest = data + img->info_size;
	img = mmi->src_img;
	DASSERT (img);

	uint wd = img->width;
	uint ht = img->height;
	uint ni;
	for (ni = 0; ni < mmi->n_mipmap; ni++)
	{
		if (img->mipmap)
			img = img->mipmap;

		wd /= 2;
		ht /= 2;
		DASSERT (wd && ht);
		err = ResizeIMG (&temp, false, img, wd, ht);
		if (err)
			break;
		// PRINT("RESIZE  %3u*%-3u ",wd,ht); HEXDUMP16(0,0,temp.data,16);

		err = ConvertIMG (&temp, false, 0, mmi->img.iform, mmi->img.pform);
		if (err)
			break;
		// PRINT("CONVERT %3u*%-3u ",wd,ht); HEXDUMP16(0,0,temp.data,16);

		DASSERT (dest + temp.data_size <= data + mmi->image_size);
		memcpy (dest, temp.data, temp.data_size);
		dest += temp.data_size;
	}
	if (p_dest)
		*p_dest = dest;

	ResetIMG (&temp);
	ResetIMG (&mmi->img);
	return err;
}
enumError SaveImageBuffer (Image_t *img, FILE *fo, ccp path, bool overwrite,
	const u8 *data, uint size, ccp format)
{
	enumError err;
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
			return err;
		}
	}

	if (fwrite (data, 1, size, f.f) != size)
	{
		err = ERROR0 (ERR_WRITE_FAILED, "Error while writing %s data: %s\n", format, path);
		RegisterFileError (&f, ERR_WRITE_FAILED);
	}
	else
		err = ERR_OK;

	if (opt_preserve)
		memcpy (&f.fatt, &img->fatt, sizeof (f.fatt));
	if (fo)
		f.f = 0;
	return ResetFile (&f, opt_preserve) ?: err;
}

//-----------------------------------------------------------------------------

enumError SaveIMG (Image_t *img, // pointer to valid img
	file_format_t fform, // file format
	const MipmapOptions_t *mmo, // NULL or mipmap options
	FILE *f, // output file, if NULL then use fname+overwrite
	ccp fname, // filename of source
	bool overwrite // true: force overwriting
)
{
	DASSERT (img);
	DASSERT (fname);

	PRINT ("SaveIMG(N=%u,o=%d) %s, mo=%s, %s\n", CountMipmapsIMG (img), overwrite,
		PrintFormat3 (fform, img->iform, img->pform), InfoMipmapOptions (mmo), fname);

	switch (fform)
	{
			// [[tpl-ex+]]
		case FF_CUPICON:
		case FF_TPL:
		case FF_TPLX:
			return SaveTPL (img, fform, f, fname, overwrite);
		case FF_BTI:
			return SaveBTI (img, mmo, f, fname, overwrite);
		case FF_TEX:
			return SaveTEX (img, mmo, f, fname, overwrite, false);
		case FF_TEX_CT:
			return SaveTEX (img, mmo, f, fname, overwrite, true);
		case FF_BREFT:
		case FF_BREFT_IMG:
			return SaveBREFTIMG (img, mmo, f, fname, overwrite);
		case FF_PNG:
			return SavePNG (img, true, f, fname, 0, 0, overwrite, 0);
		case FF_AJPG:
			return SaveAJPG (img, f, fname, 0, overwrite);
		case FF_CTXB:
			return SaveCTXB (img, f, fname, overwrite);
		case FF_BTGA:
		case FF_DMPBM:
		case FF_STEX:
		case FF_CMB:
			return SavePica3DSTexture (img, fform, f, fname, overwrite);
		case FF_SMDH:
			return SaveSMDH (img, f, fname, overwrite);
		case FF_NUT:
			return SaveNUT (img, f, fname, overwrite);
		case FF_NSBTX:
			return SaveNSBTX (img, f, fname, overwrite);
		case FF_DDS:
			return SaveDDSFile (img, f, fname, overwrite);
		case FF_ASTC:
			return SaveASTCFile (img, f, fname, overwrite);
		case FF_MPT:
			return SaveMPT (img, f, fname, overwrite);

		default:
			return ERROR0 (ERR_INVALID_IFORM, "Can_t create image [file type=%s]: %s\n",
				GetNameFF (0, fform), fname);
	}
}


enumError encode_image_from_png (ccp png_path, ccp dest_path)
{
	Image_t img;
	enumError err = LoadIMG (&img, true, png_path, 0, false, true, false);
	if (err || !img.data)
		return err ? err : ERR_NOTHING_TO_DO;

	ccp dot = strrchr (dest_path, '.');
	if (!dot)
	{
		ResetIMG (&img);
		return ERR_NOTHING_TO_DO;
	}

	// If target exists, inherit its internal pixel format / palette format.
	// Resolve the freshly decoded PNG (still raw IMG_X_* here) to a real,
	// correctly packed format first -- stomping img.iform directly (as this
	// used to do) short-circuits Transform2InternIMG()/ExecTransformIMG(),
	// which key off the raw IMG_X_* tag: with iform already overwritten to a
	// concrete format, they silently no-op and the still-raw pixel buffer
	// gets written out mislabeled as the target format, corrupting the
	// image. Do the real conversion here instead so callers below always
	// receive fully packed pixel data.
	Image_t orig_img;
	if (LoadIMG (&orig_img, true, dest_path, 0, false, true, false) == ERR_OK)
	{
		const image_format_t inherited_iform = orig_img.iform;
		const palette_format_t inherited_pform = orig_img.pform;
		ResetIMG (&orig_img);

		Transform2InternIMG (&img);
		err = ExecTransformIMG (&img);
		if (err)
		{
			ResetIMG (&img);
			return err;
		}

		if (inherited_iform != IMG_INVALID && inherited_iform != img.iform)
			err = ConvertIMG (&img, false, 0, inherited_iform,
				inherited_pform != PAL_INVALID ? inherited_pform : img.pform);
		else if (inherited_pform != PAL_INVALID && inherited_pform != img.pform)
			err = ConvertIMG (&img, false, 0, img.iform, inherited_pform);
		if (err)
		{
			ResetIMG (&img);
			return err;
		}
	}

	if (!strcasecmp (dot, ".tpl"))
	{
		if (img.iform == IMG_INVALID || img.iform == IMG_X_RGB)
			img.iform = IMG_CMPR;
		err = SaveTPL (&img, FF_TPL, 0, dest_path, true);
	}
	else if (!strcasecmp (dot, ".bti"))
	{
		if (img.iform == IMG_INVALID || img.iform == IMG_X_RGB)
			img.iform = IMG_CMPR;
		err = SaveBTI (&img, 0, 0, dest_path, true);
	}
	else if (!strcasecmp (dot, ".tex0") || !strcasecmp (dot, ".tex"))
	{
		if (img.iform == IMG_INVALID || img.iform == IMG_X_RGB)
			img.iform = IMG_CMPR;
		err = SaveTEX (&img, 0, 0, dest_path, true, false);
	}
	else if (!strcasecmp (dot, ".breft"))
	{
		err = SaveBREFTIMG (&img, 0, 0, dest_path, true);
	}
	else if (!strcasecmp (dot, ".bflim") || !strcasecmp (dot, ".bclim"))
	{
		const bool bclim = !strcasecmp (dot, ".bclim");
		Transform2XIMG (&img);
		if (img.iform == IMG_X_RGB)
		{
			const uint rgba_size = img.width * img.height * 4;
			u8 *rgba = MALLOC (rgba_size);
			if (rgba)
			{
				for (uint y = 0; y < img.height; y++)
					memcpy (rgba + 4 * y * img.width, img.data + 4 * y * img.xwidth, 4 * img.width);
				u8 *data = 0;
				uint size = 0;
				err = EncodeFLIM_RGBA (&data, &size, rgba, img.width, img.height, bclim);
				FREE (rgba);
				if (!err && data)
				{
					File_t F;
					err = CreateFileOpt (&F, true, dest_path, false, dest_path);
					if (F.f && fwrite (data, 1, size, F.f) != size)
						err = FILEERROR1 (
							&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest_path);
					ResetFile (&F, opt_preserve);
					FREE (data);
				}
			}
		}
	}
	else if (!strcasecmp (dot, ".ncgr"))
	{
		Transform2XIMG (&img);
		if (img.iform == IMG_X_RGB)
		{
			const uint rgba_size = img.width * img.height * 4;
			u8 *rgba = MALLOC (rgba_size);
			if (rgba)
			{
				for (uint y = 0; y < img.height; y++)
					memcpy (rgba + 4 * y * img.width, img.data + 4 * y * img.xwidth, 4 * img.width);
				u8 *data = 0;
				uint size = 0;
				err = EncodeNCGR_RGBA (&data, &size, rgba, img.width, img.height, true);
				FREE (rgba);
				if (!err && data)
				{
					File_t F;
					err = CreateFileOpt (&F, true, dest_path, false, dest_path);
					if (F.f && fwrite (data, 1, size, F.f) != size)
						err = FILEERROR1 (
							&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest_path);
					ResetFile (&F, opt_preserve);
					FREE (data);
				}
			}
		}
	}
	else if (!strcasecmp (dot, ".nclr"))
	{
		Transform2XIMG (&img);
		if (img.iform == IMG_X_RGB)
		{
			const uint rgba_size = img.width * img.height * 4;
			u8 *rgba = MALLOC (rgba_size);
			if (rgba)
			{
				for (uint y = 0; y < img.height; y++)
					memcpy (rgba + 4 * y * img.width, img.data + 4 * y * img.xwidth, 4 * img.width);
				u8 *data = 0;
				uint size = 0;
				err = EncodeNCLR_RGBA (&data, &size, rgba, img.width, img.height);
				FREE (rgba);
				if (!err && data)
				{
					File_t F;
					err = CreateFileOpt (&F, true, dest_path, false, dest_path);
					if (F.f && fwrite (data, 1, size, F.f) != size)
						err = FILEERROR1 (
							&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest_path);
					ResetFile (&F, opt_preserve);
					FREE (data);
				}
			}
		}
	}
	else
	{
		file_format_t ff = GetImageFFByFName (dest_path, FF_UNKNOWN, false);
		if (ff != FF_UNKNOWN)
			err = SaveIMG (&img, ff, 0, 0, dest_path, true);
		else
			err = ERR_NOTHING_TO_DO;
	}

	ResetIMG (&img);
	return err;
}

