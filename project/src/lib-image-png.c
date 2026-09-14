
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
#include <png.h>
#include "lib-image.h"
#include "lib-image-internal.h"

#ifdef TEST
#define ENABLE_IMAGE_TYPE_LOG 0 // 0|1
#define ENABLE_EXPORT_TIMER 0 // 0|1|2
#else
#define ENABLE_IMAGE_TYPE_LOG 0
#define ENABLE_EXPORT_TIMER 0
#endif

///////////////////////////////////////////////////////////////////////////////
///////////////			PNG callback functions		///////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct png_info_t
{
	ccp head_msg; // header for error messages
	File_t *file; // file pointer
	u8 *cache; // cached data
	uint cache_size; // size of 'cache' data

} png_info_t;

///////////////////////////////////////////////////////////////////////////////

static void print_png_message (enumError err, png_structp png_ptr, ccp message)
{
	const png_info_t *pinfo = png_get_error_ptr (png_ptr);
	if (!pinfo)
		ERROR0 (err, "PNG error: %s", message);
	else if (pinfo->file)
	{
		ERROR0 (err, "%s: %s: %s", pinfo->head_msg, message, pinfo->file->fname);
		RegisterFileError (pinfo->file, err);
		ResetFile (pinfo->file, false);
	}
	else
		ERROR0 (err, "%s: %s", pinfo->head_msg, message);
}

///////////////////////////////////////////////////////////////////////////////

static void png_warning_func (png_structp png_ptr, ccp message)
{
	print_png_message (ERR_PNG, png_ptr, message);
	longjmp (png_jmpbuf (png_ptr), 1);
}

///////////////////////////////////////////////////////////////////////////////

static void png_error_func (png_structp png_ptr, ccp message)
{
	print_png_message (ERR_PNG, png_ptr, message);
	longjmp (png_jmpbuf (png_ptr), 1);
}

///////////////////////////////////////////////////////////////////////////////

static void png_read_func (png_structp png_ptr, png_bytep dest, png_size_t size)
{
	png_info_t *pinfo = png_get_io_ptr (png_ptr);
	DASSERT (pinfo);
	if (pinfo->cache_size)
	{
		const uint max_read = size < pinfo->cache_size ? size : pinfo->cache_size;
		memcpy (dest, pinfo->cache, max_read);
		pinfo->cache += max_read;
		pinfo->cache_size -= max_read;
		dest += max_read;
		size -= max_read;
		errno = 0;
	}

	if (size > 0)
		fread (dest, 1, size, pinfo->file->f);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			LoadPNG()			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError LoadPNG (Image_t *img, // destination image
	bool init_img, // true: initialize 'img' first
	bool mipmaps, // true: try to load mipmaps

	ccp path1, // NULL or part #1 of path
	ccp path2 // NULL or part #2 of path
)
{
	DASSERT (img);
	PRINT ("LoadPNG(mm=%d) %s / %s\n", mipmaps, path1 ? path1 : "", path2 ? path2 : "");

	if (init_img)
		InitializeIMG (img);
	else
		ResetIMG (img);

	//--- open file

	char pathbuf[PATH_MAX];
	ccp path = PathCatPP (pathbuf, sizeof (pathbuf), path1, path2);

	File_t f;
	enumError err = OpenFILE (&f, true, path, false, false);
	if (err)
	{
		ResetFile (&f, false);
		return err;
	}

	err = ReadPNG (img, mipmaps, &f, 0, 0);

	img->path = f.fname;
	f.fname = 0;
	img->path_alloced = true;
	memcpy (&img->fatt, &f.fatt, sizeof (img->fatt));

	ResetFile (&f, false);
	return err;
}

//
///////////////////////////////////////////////////////////////////////////////

enumError ReadPNG (Image_t *img, // destination image
	bool mipmaps, // true: try to load mipmaps
	File_t *f, // valid opened file
	u8 *cache, // pre read data
	uint cache_size // size of 'cache'
)
{
	DASSERT (img);
	DASSERT (f);
	DASSERT (f->f);

	//--- setup png

	png_info_t pinfo;
	memset (&pinfo, 0, sizeof (pinfo));
	pinfo.head_msg = "Open PNG";
	pinfo.file = f;
	pinfo.cache = cache;
	pinfo.cache_size = cache_size;

	png_structp png_ptr
		= png_create_read_struct (PNG_LIBPNG_VER_STRING, &pinfo, png_error_func, png_warning_func);
	if (!png_ptr)
		goto abort_init;

	png_infop info_ptr = png_create_info_struct (png_ptr);
	if (!info_ptr)
	{
		png_destroy_read_struct (&png_ptr, 0, 0);
		goto abort_init;
	}

	png_infop end_info = png_create_info_struct (png_ptr);
	if (!end_info)
	{
		png_destroy_read_struct (&png_ptr, &info_ptr, 0);
		goto abort_init;
	}

	if (setjmp (png_jmpbuf (png_ptr)))
	{
		png_destroy_read_struct (&png_ptr, &info_ptr, &end_info);
		PRINT ("ERR=%u\n", f->max_err);
		return f->max_err;
	}

	if (pinfo.cache_size)
	{
		DASSERT (pinfo.cache);
		png_set_read_fn (png_ptr, &pinfo, png_read_func);
	}
	else
		png_init_io (png_ptr, f->f);

	png_read_info (png_ptr, info_ptr);

	u32 width, height;
	int bit_depth, color_type, interlace;
	png_get_IHDR (png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace, 0, 0);
	noPRINT ("--> %u*%u*%u, ct=%d, il=%d\n", width, height, bit_depth, color_type, interlace);

	if (interlace != PNG_INTERLACE_NONE)
	{
		png_destroy_read_struct (&png_ptr, &info_ptr, &end_info);
		RegisterFileError (f, ERR_INVALID_IFORM);
		return ERROR0 (ERR_INVALID_IFORM, "Interlaced PNG not supported: %s\n", f->fname);
	}

	uint data_size;
	bool is_gray;
	img->alpha_status = -1;

	switch (color_type)
	{
		case PNG_COLOR_TYPE_GRAY_ALPHA:
			img->alpha_status = 0;
		case PNG_COLOR_TYPE_GRAY:
			is_gray = true;
			data_size = EXPAND8 (width) * EXPAND8 (height) * 2;
			img->iform = IMG_X_GRAY;
			break;

		case PNG_COLOR_TYPE_RGB_ALPHA:
		case PNG_COLOR_TYPE_PALETTE:
			img->alpha_status = 0;
		case PNG_COLOR_TYPE_RGB:
			is_gray = false;
			data_size = EXPAND8 (width) * EXPAND8 (height) * 4;
			img->iform = IMG_X_RGB;
			break;

		default:
			png_destroy_read_struct (&png_ptr, &info_ptr, &end_info);
			ERROR0 (ERR_INVALID_IFORM, "Unsupported PNG color type: %s\n", f->fname);
			RegisterFileError (f, ERR_INVALID_IFORM);
			return RegisterFileError (f, ERR_INVALID_IFORM);
	}

	if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb (png_ptr);

	if (png_get_valid (png_ptr, info_ptr, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha (png_ptr);

	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
		png_set_expand_gray_1_2_4_to_8 (png_ptr);

	if (bit_depth == 16)
		png_set_strip_16 (png_ptr);

	png_set_add_alpha (png_ptr, 0xff, PNG_FILLER_AFTER);
	png_read_update_info (png_ptr, info_ptr);

	u8 *data = MALLOC (data_size);

	img->data = data;
	img->data_alloced = true;
	img->data_size = data_size;
	img->width = width;
	img->height = height;
	img->xwidth = EXPAND8 (width);
	img->xheight = EXPAND8 (height);
	img->info_iform = img->iform;
	img->info_fform = FF_PNG;

	//--- read data

	const uint rowlen = (is_gray ? 2 : 4) * img->xwidth;

#if HAVE_PRINT
	png_get_IHDR (png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace, 0, 0);
	noPRINT ("--> %u*%u*%u, ct=%d, il=%d\n", width, height, bit_depth, color_type, interlace);

	const uint channels = png_get_channels (png_ptr, info_ptr);
	const uint rowbytes = png_get_rowbytes (png_ptr, info_ptr);

	PRINT ("ReadPNG() %u*%u, ch=%u, gray=%d, alpha=%d, rl=%u,%u\n", width, height, channels,
		is_gray, img->alpha_status, rowbytes, rowlen);
#endif

	while (height-- > 0)
	{
		png_read_row (png_ptr, (png_bytep)data, NULL);
		data += rowlen;
	}
	DASSERT (data <= img->data + data_size);

	//--- close png and file

	png_read_end (png_ptr, info_ptr);
	png_destroy_read_struct (&png_ptr, &info_ptr, &end_info);

	//--- mipmap support

	if (mipmaps)
	{
		ccp ext = strrchr (f->fname, '.');
		ccp file = strrchr (f->fname, '/');
		if (!ext || file && ext < file)
			ext = f->fname + strlen (f->fname);

		Image_t *mm_img = img;
		uint count;
		for (count = 1;; count++)
		{
			char mm_path[PATH_MAX];
			snprintf (mm_path, sizeof (mm_path), "%.*s.mm%u%s", (int)(ext - f->fname), f->fname,
				count, ext);
			PRINT ("Try open PNG: %s\n", mm_path);
			File_t F;
			enumError err = OpenFILE (&F, true, mm_path, true, false); // local err
			if (err)
			{
				ResetFile (&F, false);
				if (err != ERR_NOT_EXISTS)
					return err;
				break;
			}

			mm_img->mipmap = MALLOC (sizeof (*mm_img->mipmap));
			mm_img = mm_img->mipmap;
			InitializeIMG (mm_img);
			err = ReadPNG (mm_img, false, &F, 0, 0);
			memcpy (&mm_img->fatt, &F.fatt, sizeof (mm_img->fatt));
			ResetFile (&F, false);
			if (err)
				return err;
		}
		MM_COUNT (img);
	}

	return PatchListIMG (img);

	//--- abort

abort_init:
	ERROR0 (ERR_READ_FAILED, "Error while initializing PNG data: %s\n", f->fname);
	return RegisterFileError (f, ERR_READ_FAILED);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			SavePNG()			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError SavePNG (Image_t *img, // valid image
	bool mipmaps, // true: save mipmaps (auto file name)
	FILE *fo, // output file, if NULL then use path1+path2
	ccp path1, // NULL or part #1 of path
	ccp path2, // NULL or part #2 of path
	int store_alpha, // <0:no alpha, =0:auto alpha, >0:store alpha
	bool overwrite, // true: force overwriting
	StringField_t *file_list // not NULL: store filenames of created png files

)
{
	DASSERT (img);

	//--- setup path & ...

	if (!path2 || !*path2)
	{
		path2 = path1;
		path1 = 0;
	}

	char pathbuf[PATH_MAX];
	ccp path = PathCatPP (pathbuf, sizeof (pathbuf), path1, path2);
	PRINT ("SavePNG(mm=%d/%u) {%u,%u} alpha=%d : %s\n", mipmaps, CountMipmapsIMG (img),
		img->conv_count, img->seq_num, store_alpha, path);

	Transform2XIMG (img);
	enumError err = ExecTransformIMG (img);
	if (err)
		return err;

	if (!store_alpha)
		store_alpha = CheckAlphaIMG (img, false);

	if (GetPaletteCountIF (img->iform) && (store_alpha > 0 || img->n_pal > 0x100))
	{
		err = ConvertToRGB (img, img, PAL_AUTO);
		if (err)
			return err;
	}

	//--- setup export mode

	enum export_mode
	{
		MD_F_ALPHA = 1,
		MD_F_RGB = 2,
		MD_F_PAL = 4,

		MD_G = 0,
		MD_GA = MD_F_ALPHA,
		MD_RGB = MD_F_RGB,
		MD_RGBA = MD_F_RGB | MD_F_ALPHA,
		MD_PAL = MD_F_PAL,

	} export_mode;

	switch (img->iform)
	{
		case IMG_X_GRAY:
			export_mode = store_alpha < 0 ? MD_G : MD_GA;
			break;

		case IMG_X_RGB:
			export_mode = store_alpha < 0 ? MD_RGB : MD_RGBA;
			break;

		case IMG_X_PAL:
		case IMG_X_PAL4:
		case IMG_X_PAL8:
		case IMG_X_PAL14:
			export_mode = MD_PAL;
			break;

		default:
			return ERROR0 (ERR_INVALID_IFORM,
				"Image format 0x%02x [%s] not supported for PNG export: %s\n", img->iform,
				GetImageFormatName (img->iform, "?"), path);
	}

	//--- create file

	if (file_list)
	{
		ccp str = path + (path1 ? strlen (path1) : 0);
		if (*str == '/')
			str++;
		InsertStringField (file_list, str, false);
	}

	File_t f;
	if (fo)
	{
		InitializeFile (&f);
		f.f = fo;
		f.is_writing = true;
		mipmaps = false;
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

	//--- setup png

	png_info_t pinfo;
	memset (&pinfo, 0, sizeof (pinfo));
	pinfo.head_msg = "Create PNG";
	pinfo.file = &f;

	png_structp png_ptr
		= png_create_write_struct (PNG_LIBPNG_VER_STRING, &pinfo, png_error_func, png_warning_func);
	if (!png_ptr)
		goto abort_init;

	png_infop info_ptr = png_create_info_struct (png_ptr);
	if (!info_ptr)
	{
		png_destroy_write_struct (&png_ptr, 0);
		goto abort_init;
	}

	if (setjmp (png_jmpbuf (png_ptr)))
	{
		png_destroy_write_struct (&png_ptr, &info_ptr);
		goto abort;
	}

	png_init_io (png_ptr, f.f);

	DASSERT (EXPAND8 (img->width) == img->xwidth);
	DASSERT (EXPAND8 (img->height) == img->xheight);

	switch (export_mode)
	{
		case MD_G:
			png_set_IHDR (png_ptr, info_ptr, img->width, img->height, 8, PNG_COLOR_TYPE_GRAY,
				PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
			break;

		case MD_GA:
			png_set_IHDR (png_ptr, info_ptr, img->width, img->height, 8, PNG_COLOR_TYPE_GRAY_ALPHA,
				PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
			break;

		case MD_RGB:
			png_set_IHDR (png_ptr, info_ptr, img->width, img->height, 8, PNG_COLOR_TYPE_RGB,
				PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
			break;

		case MD_RGBA:
			png_set_IHDR (png_ptr, info_ptr, img->width, img->height, 8, PNG_COLOR_TYPE_RGB_ALPHA,
				PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
			break;

		case MD_PAL:
			png_set_IHDR (png_ptr, info_ptr, img->width, img->height, 8, PNG_COLOR_TYPE_PALETTE,
				PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

			// iobuf to transform palette into 'no alpha'
			{
				png_colorp dest = (png_colorp)iobuf;
				DASSERT (img->n_pal * sizeof (*dest) <= sizeof (iobuf));
				const u8 *src = img->pal;
				uint c = img->n_pal;
				while (c-- > 0)
				{
					dest->red = *src++;
					dest->green = *src++;
					dest->blue = *src++;
					src++;
					dest++;
				}
				png_set_PLTE (png_ptr, info_ptr, (png_colorp)iobuf, img->n_pal);
			}
			break;
	}

	//---- write strings

	png_text ptext[5], *pt = ptext;
	memset (ptext, 0, sizeof (ptext));

	if (!opt_strip)
	{
		pt->compression = PNG_TEXT_COMPRESSION_NONE;
		pt->key = "creator";
		pt->text = TOOLSET_LONG;
		pt->text_length = strlen (pt->text);
		pt++;

		if (img->info_fform > FF_UNKNOWN)
		{
			pt->compression = PNG_TEXT_COMPRESSION_NONE;
			pt->key = "file-format";
			pt->text = (char *)GetNameFF (0, img->info_fform);
			pt->text_length = strlen (pt->text);
			pt++;
		}

		ccp info_text = GetImageFormatName (img->info_iform, 0);
		if (info_text)
		{
			pt->compression = PNG_TEXT_COMPRESSION_NONE;
			pt->key = "image-format";
			pt->text = (char *)info_text;
			pt->text_length = strlen (pt->text);
			pt++;
		}

		info_text = GetPaletteFormatName (img->info_pform, 0);
		if (info_text)
		{
			pt->compression = PNG_TEXT_COMPRESSION_NONE;
			pt->key = "palette-format";
			pt->text = (char *)info_text;
			pt->text_length = strlen (pt->text);
			pt++;
		}
	}

	DASSERT (pt - ptext <= sizeof (ptext) / sizeof (*ptext));
	png_set_text (png_ptr, info_ptr, ptext, pt - ptext);
	png_write_info (png_ptr, info_ptr);

	//--- write image data

	switch (export_mode)
	{
		case MD_G:
		{
			if (img->xwidth > sizeof (iobuf))
				goto abort_init;

			const u8 *data = img->data;
			uint ih = img->height;
			while (ih-- > 0)
			{
				u8 *dest = (u8 *)iobuf;
				uint iw = img->xwidth;
				while (iw-- > 0)
				{
					*dest++ = *data++;
					data++;
				}
				png_write_row (png_ptr, (png_bytep)iobuf);
			}
		}
		break;

		case MD_GA:
		{
			const uint rowlen = img->xwidth * 2;
			const u8 *data = img->data;
			uint ih = img->height;
			while (ih-- > 0)
			{
				png_write_row (png_ptr, (png_bytep)data);
				data += rowlen;
			}
		}
		break;

		case MD_RGB:
		{
			if (img->xwidth * 3 > sizeof (iobuf))
				goto abort_init;

			const u8 *data = img->data;
			uint ih = img->height;
			while (ih-- > 0)
			{
				u8 *dest = (u8 *)iobuf;
				uint iw = img->xwidth;
				while (iw-- > 0)
				{
					*dest++ = *data++;
					*dest++ = *data++;
					*dest++ = *data++;
					data++;
				}
				png_write_row (png_ptr, (png_bytep)iobuf);
			}
		}
		break;

		case MD_RGBA:
		{
			const uint rowlen = img->xwidth * 4;
			const u8 *data = img->data;
			uint ih = img->height;
			while (ih-- > 0)
			{
				png_write_row (png_ptr, (png_bytep)data);
				data += rowlen;
			}
		}
		break;

		case MD_PAL:
		{
			u8 *dest_end = (u8 *)iobuf + img->xwidth;
			const u16 *data = (u16 *)img->data;
			uint ih = img->height;
			while (ih-- > 0)
			{
				// transform index into single byte
				u8 *dest = (u8 *)iobuf;
				while (dest < dest_end)
					*dest++ = *data++;
				png_write_row (png_ptr, (png_bytep)iobuf);
			}
		}
		break;
	}

	//--- close png and file

	if (setjmp (png_jmpbuf (png_ptr)))
	{
		png_destroy_write_struct (&png_ptr, &info_ptr);
		goto abort;
	}

	png_write_end (png_ptr, info_ptr);
	png_destroy_write_struct (&png_ptr, &info_ptr);

	if (opt_preserve)
		memcpy (&f.fatt, &img->fatt, sizeof (f.fatt));

	if (fo)
		f.f = 0;
	err = ResetFile (&f, opt_preserve);

	if (mipmaps && img->mipmap)
	{
		ccp ext = strrchr (path2, '.');
		ccp file = strrchr (path2, '/');
		if (!ext || file && ext < file)
			ext = path2 + strlen (path2);

		uint count;
		for (count = 1; !err; count++)
		{
			img = img->mipmap;
			if (!img)
				break;

			char mm_path[PATH_MAX];
			snprintf (
				mm_path, sizeof (mm_path), "%.*s.mm%u%s", (int)(ext - path2), path2, count, ext);

			PRINT ("## SAVE -> %s\n", mm_path);
			err = SavePNG (img, false, 0, path1, mm_path, store_alpha, overwrite, file_list);
		}
	}
	return err;

	//--- abort

abort_init:
	ERROR0 (ERR_WRITE_FAILED, "Error while initializing PNG data: %s\n", path);
abort:
	RegisterFileError (&f, ERR_WRITE_FAILED);
	return ResetFile (&f, false);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			ExportPNG			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError ExportPNG (ccp path1, // NULL or part #1 of path
	ccp path2, // NULL or part #2 of path
	FileAttrib_t *fatt, // NULL or file attributes
	const void *img_data, // image data
	uint img_size, // image size, needed for validation
	uint img_index, // index of sub image, 0:main, >0:mipmaps
	bool mipmaps, // true: load and export mipmaps
	uint *n_image, // not null: store detected image count
	const endian_func_t *endian, // endian functions to read data
	FormatFieldItem_t *ffi, // not null: store detected image+pal format
	bool create_png, // false: do some calculations but don't create png
	StringField_t *file_list, // not NULL: store filenames of created png files
	palette_format_t ext_pform, // PAL_INVALID: no external palette
	uint ext_n_pal, const u8 *ext_pal)
{
	DASSERT (img_data);
	DASSERT (endian);

	char pathbuf[PATH_MAX];
	ccp path = PathCatPP (pathbuf, sizeof (pathbuf), path1, path2);
	TRACE ("ExportPNG(size=%u) %s\n", img_size, path);

#if ENABLE_EXPORT_TIMER
	static u64 total_time = 0;
	u64 start_time = GetTimerUSec ();
#endif

	Image_t img;
	enumError err = AssignIMG (&img, 1, img_data, img_size, img_index, mipmaps, endian, path);

	// BRRES TEX0 (and similar containers) carry no palette of their own --
	// AssignIMG() leaves img.pform == PAL_INVALID for an indexed iform in
	// that case. A caller that resolved the sibling PLT0 hands its raw,
	// still-encoded palette bytes through here; only step in when the
	// format actually needs one and AssignIMG() didn't already find one.
	if (!err && ext_pal && img.pform == PAL_INVALID && GetPaletteCountIF (img.iform))
	{
		img.pal = (u8 *)ext_pal;
		img.pal_size = ext_n_pal * 2;
		img.pal_alloced = false;
		img.n_pal = ext_n_pal;
		img.pform = ext_pform;
		img.info_pform = ext_pform;
	}

	if (n_image)
		*n_image = img.info_n_image;
	if (ffi)
	{
		ffi->iform = img.iform;
		ffi->pform = img.pform;
	}
	if (fatt)
		memcpy (&img.fatt, fatt, sizeof (img.fatt));

	if (!err)
	{
#if ENABLE_IMAGE_TYPE_LOG
		{
			static bool log_opened = false;
			static FILE *log = 0;
			if (!log_opened)
			{
				log_opened = true;
				ccp fname
					= IsDirectory ("pool", false) ? "pool/_image-format.log" : "_image-format.log";
				log = fopen (fname, "wb");
				if (log)
					printf (">>> IMAGE LOG OPENED: %s <<<\n", fname);
			}
			if (log)
				fprintf (log, "%02x [%-5s %-6s] : %4u * %4u : %s\n", iform, GetNameFF (0, fform),
					GetImageFormatName (iform, "?"), img.width, img.height, path);
		}
#endif

		if (create_png)
		{
			err = ConvertIMG (&img, false, 0, IMG_X_AUTO, PAL_INVALID);
			if (!err)
				err = SavePNG (&img, mipmaps, 0, path1, path2, 0, false, file_list);

#if ENABLE_EXPORT_TIMER
			start_time = GetTimerUSec () - start_time;
			total_time += start_time;
			printf ("\t\t--> TIME: %s [%s total]\n",
				PrintUSec (0, 0, start_time, ENABLE_EXPORT_TIMER),
				PrintUSec (0, 0, total_time, ENABLE_EXPORT_TIMER));
#endif
		}
	}
	ResetIMG (&img);
	return err;
}

//
///////////////////////////////////////////////////////////////////////////////
// Public wrapper around InitializeIMG+AssignDecodedRGBA+SavePNG+ResetIMG for
// callers outside this file that already have a decoded RGBA8 buffer (e.g.
// wszst.c writing FTEX textures found inside a Wii U BFRES as sibling PNGs
// during extraction) and just want it on disk, without building their own
// Image_t. 'rgba' must be dclib-allocated; ownership transfers in either case.
enumError SaveDecodedRGBAToPNG (u8 *rgba, uint width, uint height, const endian_func_t *endian,
	ccp path1, ccp path2, bool overwrite)
{
	Image_t img;
	InitializeIMG (&img);
	AssignDecodedRGBA (&img, rgba, width, height, endian, path2 ? path2 : path1);
	const enumError err = SavePNG (&img, false, 0, path1, path2, 0, overwrite, 0);
	ResetIMG (&img);
	return err;
}
