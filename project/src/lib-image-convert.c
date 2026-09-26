
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
#include "lib-cmpr.h"

// Shared with lib-image-core.c: see the extern declaration there.
int convert_depth = 0;

//
///////////////////////////////////////////////////////////////////////////////
///////////////			conversion helpers		///////////////
///////////////////////////////////////////////////////////////////////////////

u8 *AllocDataIMG (const Image_t *src, // image template (read width and height)
	uint bytes_per_pix, // number of bytes per pixel
	uint *data_size // not NULL: store data size
)
{
	DASSERT (src);

	const uint size = EXPAND8 (src->width) * EXPAND8 (src->height) * bytes_per_pix;
	if (data_size)
		*data_size = size;
	return CALLOC (1, size);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void AssignDataGRAY (Image_t *dest, // valid destination
	const Image_t *src, // NULL or image template
	u8 *data // with AllocDataIMG() alloced data
)
{
	DASSERT (dest);
	DASSERT (data);

	FreeIMG (dest, src);

	dest->data = data;
	dest->data_alloced = true;
	dest->xwidth = EXPAND8 (dest->width);
	dest->xheight = EXPAND8 (dest->height);
	dest->data_size = dest->xwidth * dest->xheight * 2;
	dest->iform = IMG_X_GRAY;
	dest->is_grayed = true;
}

///////////////////////////////////////////////////////////////////////////////

void AssignDataRGB (Image_t *dest, // valid destination
	const Image_t *src, // NULL or image template
	u8 *data // with AllocDataIMG() alloced data
)
{
	DASSERT (dest);
	DASSERT (data);

	FreeIMG (dest, src);

	dest->data = data;
	dest->data_alloced = true;
	dest->xwidth = EXPAND8 (dest->width);
	dest->xheight = EXPAND8 (dest->height);
	dest->data_size = dest->xwidth * dest->xheight * 4;
	dest->iform = IMG_X_RGB;
}

///////////////////////////////////////////////////////////////////////////////

static void AssignDataPAL (Image_t *dest, // valid destination
	const Image_t *src, // NULL or image template
	u16 *itab, // with AllocDataIMG() alloced data
	u8 *pal_data, // palette data
	uint size_pal, // NULL or size of 'pal_data'
	uint n_pal, // number of used palette entries
	image_format_t iform, // palette image format
	int alpha_status // alpha status of image
)
{
	DASSERT (dest);
	DASSERT (itab);
	DASSERT (pal_data);
	DASSERT (iform >= IMG_X_PAL__MIN && iform <= IMG_X_PAL__MAX);

	FreeIMG (dest, src);

	dest->data = (u8 *)itab;
	dest->data_alloced = true;
	dest->xwidth = EXPAND8 (dest->width);
	dest->xheight = EXPAND8 (dest->height);
	dest->data_size = dest->xwidth * dest->xheight * 2;
	dest->iform = iform;
	dest->alpha_status = alpha_status;

	dest->pform = PAL_X_RGB;
	dest->pal = pal_data;
	dest->pal_size = size_pal ? size_pal : n_pal * 4;
	dest->pal_alloced = true;
	dest->n_pal = n_pal;
}

///////////////////////////////////////////////////////////////////////////////

void AssignData (Image_t *dest, // valid destination
	const Image_t *src, // NULL or image template
	u8 *data, // alloced data
	uint data_size, // size of 'data'
	image_format_t iform // image format
)
{
	DASSERT (dest);
	DASSERT (data);

	FreeIMG (dest, src);

	dest->data = data;
	dest->data_alloced = true;
	dest->data_size = data_size;
	dest->xwidth = EXPAND8 (dest->width);
	dest->xheight = EXPAND8 (dest->height);
	dest->iform = iform;
	dest->pform = PAL_INVALID;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

enumError CalcImageBlock (const Image_t *img, // image template (read width and height)
	uint bits_per_pixel, // number of bits per pixel
	uint block_width, // width of a single block
	uint block_height, // height of a single block

	uint *h_blocks, // not NULL: store number of horizontal blocks
	uint *v_blocks, // not NULL: store number of vertical blocks
	uint *img_size, // not NULL: return image size

	bool calc_only // true: do no validation
)
{
	DASSERT (img);

	uint xwidth, xheight;
	const uint size = CalcImageSize (img->width, img->height, bits_per_pixel, block_width,
		block_height, &xwidth, &xheight, h_blocks, v_blocks);
	if (img_size)
		*img_size = size;

	if (calc_only || xwidth > 0 && xheight > 0 && size <= img->data_size)
		return ERR_OK;

	PRINT0 ("h=%u,%u,%u  v=%u,%u,%u, size=%u/%u <= %x/%x\n", img->width, block_width, xwidth,
		img->height, block_height, xheight, size, img->data_size, size, img->data_size);

	return ERROR0 (ERR_INVALID_IFORM, "Impossible geometry of image [0x%02x=%s]: %s\n", img->iform,
		GetImageFormatName (img->iform, "?"), img->path);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			palette helpers			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError TransformPalette (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	image_format_t iform, // new image format, only valid IMG_X_PAL*
	u16 *itab // index table (2*xwidth*xheight), alloced
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (itab);
	DASSERT (iform >= IMG_X_PAL__MIN && iform <= IMG_X_PAL__MAX);

	//--- create palette

	const uint n_idx = GetPaletteCountIF (iform);
	DASSERT (n_idx);
	DASSERT (n_idx * 4 <= sizeof (iobuf));
	u8 *pal_data = CALLOC (n_idx, 4);
	u8 *pdest = pal_data;

	u16 (*rd16) (const void *data_ptr) = src_img->endian->rd16;
	// u32 (*rd32) ( const void * data_ptr ) = src_img->endian->rd32;

	const u8 *src = src_img->pal;
	// Every case below writes exactly 4 bytes per source palette entry into
	// 'pal_data', which is sized for n_idx entries (the destination format's
	// fixed palette capacity) -- not for n_pal (the source image's own
	// declared palette count). A real texture (seen on Super Smash Bros.
	// Brawl) can carry n_pal > n_idx, which walked off the end of pal_data;
	// GetValidBTI() already treats n_pal > max_pal as invalid elsewhere
	// (lib-std.c), so clamp here too instead of trusting the source blindly.
	const uint n_pal_raw = src ? src_img->n_pal : 0;
	const uint n_pal = n_pal_raw > n_idx ? n_idx : n_pal_raw;
	uint pi;

	if (!src || n_pal == 0 || src_img->pform < 0 || src_img->pform > PAL_RGB5A3)
	{
		for (pi = 0; pi < n_idx; pi++)
		{
			u8 val = (u8)(pi * 255 / (n_idx > 1 ? n_idx - 1 : 1));
			*pdest++ = val;
			*pdest++ = val;
			*pdest++ = val;
			*pdest++ = 0xff;
		}
		AssignDataPAL (dest_img, src_img, itab, pal_data, 0, n_idx, iform, -1);
		return ERR_OK;
	}

	int alpha_status = 0; // don't know

	switch (src_img->pform)
	{
#if 0
      case PAL_I4:
	alpha_status = -1;
	for ( pi = n_pal/2; pi > 0; pi-- )
	{
	    const u8 val = *src++;
	    const u8 gray1 = cc48[ val >> 4 ];
	    *pdest++ = gray1;		// red
	    *pdest++ = gray1;		// green
	    *pdest++ = gray1;		// blue
	    *pdest++ = 0xff;		// alpha
	    const u8 gray2 = cc48[ val & 0x0f ];
	    *pdest++ = gray2;		// red
	    *pdest++ = gray2;		// green
	    *pdest++ = gray2;		// blue
	    *pdest++ = 0xff;		// alpha
	}
	break;
#endif

#if 0
      case PAL_I8:
	alpha_status = -1;
	for ( pi = n_pal; pi > 0; pi-- )
	{
	    const u8 gray = *src++;
	    *pdest++ = gray;	// red
	    *pdest++ = gray;	// green
	    *pdest++ = gray;	// blue
	    *pdest++ = 0xff;	// alpha
	}
	break;
#endif

#if 0
      case PAL_IA4:
	for ( pi = n_pal; pi > 0; pi-- )
	{
	    const u8 val = *src++;
	    const u8 gray = cc48[ val & 0x0f ];
	    *pdest++ = gray;			// red
	    *pdest++ = gray;			// green
	    *pdest++ = gray;			// blue
	    *pdest++ = cc48[ val >> 4 ];	// alpha
	}
	break;
#endif

		case PAL_IA8:
			for (pi = n_pal; pi > 0; pi--)
			{
				const u16 val = rd16 (src);
				src += 2;
				*pdest++ = val; // red
				*pdest++ = val; // green
				*pdest++ = val; // blue
				*pdest++ = val >> 8; // alpha
			}
			break;

		case PAL_RGB565:
			alpha_status = -1;
			for (pi = n_pal; pi > 0; pi--)
			{
				const u16 val = rd16 (src);
				src += 2;
				*pdest++ = cc58[val >> 11]; // red
				*pdest++ = cc68[val >> 5 & 0x3f]; // green
				*pdest++ = cc58[val & 0x1f]; // blue
				*pdest++ = 0xff; // alpha
			}
			break;

		case PAL_RGB5A3:
			for (pi = n_pal; pi > 0; pi--)
			{
				const u16 val = rd16 (src);
				src += 2;
				if (val & 0x8000)
				{
					*pdest++ = cc58[val >> 10 & 0x1f]; // red
					*pdest++ = cc58[val >> 5 & 0x1f]; // green
					*pdest++ = cc58[val & 0x1f]; // blue
					*pdest++ = 0xff; // alpha
				}
				else
				{
					*pdest++ = cc48[val >> 8 & 0x0f]; // red
					*pdest++ = cc48[val >> 4 & 0x0f]; // green
					*pdest++ = cc48[val & 0x0f]; // blue
					*pdest++ = cc38[val >> 12 & 0x07]; // alpha
				}
			}
			break;

#if 0 // [[not-needed]] not needed yet 2011-06
      case PAL_RGBA32:
	//alpha_status = 0;
	for ( pi = n_pal; pi > 0; pi-- )
	{
#if 1 // unkown, which variant will be true
	    const u32 val = rd32(src);
	    src += 4;
	    *pdest++ = val >> 16;	// red
	    *pdest++ = val >>  8;	// green
	    *pdest++ = val;		// blue
	    *pdest++ = val >> 24;	// alpha
#else
	    const u16 ar = rd16(src);
	    const u16 gb = rd16(src+n_pal*2);	// delta unclear ???
	    src += 2;
	    *pdest++ = ar;	// red
	    *pdest++ = gb >> 8;	// green
	    *pdest++ = gb;	// blue
	    *pdest++ = ar >> 8;	// alpha
#endif
	}
	break;
#endif

		default:
			FREE (pal_data);
			FREE (itab);
			return ERROR0 (ERR_INVALID_IFORM, "Palette format 0x%02x [%s] not supported: %s\n",
				src_img->pform, GetPaletteFormatName (src_img->pform, "?"), src_img->path);
	}

	//--- create image using the palette

	AssignDataPAL (dest_img, src_img, itab, pal_data, 0, n_pal, iform, alpha_status);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		    convert gray : rgb : palette	///////////////
///////////////////////////////////////////////////////////////////////////////

enumError ConvertToRGB (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	palette_format_t pform // wanted palette format => ignored
)
{
	DASSERT (dest_img);
	DASSERT (src_img);

	//--- mipmap support

	if (src_img->mipmap) // --> do the conversion for each image standalone!
	{
		if (src_img != dest_img)
			CopyIMG (dest_img, false, src_img, true); // this makes it easier to handle
		RemoveContainerIMG (dest_img);
		DASSERT (!dest_img->container);

		while (dest_img)
		{
			Image_t *next = dest_img->mipmap;
			dest_img->mipmap = 0;
			enumError err = ConvertToRGB (dest_img, dest_img, pform);
			dest_img->mipmap = next;
			if (err)
				return err;
			dest_img = next;
		}
		return ERR_OK;
	}

	//--- convert image

	if (src_img->iform == IMG_X_RGB)
	{
		PRINT ("%*sCONVERT: RGB {%u,%u} -> RGB {%u,%u}\n", convert_depth * 2, "",
			src_img->conv_count, src_img->seq_num, dest_img->conv_count, dest_img->seq_num);
		CopyIMG (dest_img, false, src_img, true);
	}
	else if (src_img->iform == IMG_X_GRAY)
	{
		PRINT ("%*sCONVERT: GRAY {%u,%u} -> RGB {%u,%u}\n", convert_depth * 2, "",
			src_img->conv_count, src_img->seq_num, dest_img->conv_count, dest_img->seq_num);

		DASSERT (EXPAND8 (src_img->width) == src_img->xwidth);
		DASSERT (EXPAND8 (src_img->height) == src_img->xheight);

		const u8 *src = src_img->data;
		const u8 *src_end = src + 2 * src_img->xwidth * src_img->xheight;

		uint data_size;
		u8 *data = AllocDataIMG (src_img, 4, &data_size);
		u8 *dest = data;

		while (src < src_end)
		{
			const u8 val = *src++;
			*dest++ = val;
			*dest++ = val;
			*dest++ = val;
			*dest++ = *src++;
		}

		DASSERT (dest == data + data_size);
		AssignDataRGB (dest_img, src_img, data);
	}
	else if (src_img->iform >= IMG_X_PAL__MIN && src_img->iform <= IMG_X_PAL__MAX)
	{
		PRINT ("%*sCONVERT: PAL {%u,%u} -> RGB {%u,%u}\n", convert_depth * 2, "",
			src_img->conv_count, src_img->seq_num, dest_img->conv_count, dest_img->seq_num);

		uint data_size;
		u8 *data = AllocDataIMG (src_img, 4, &data_size);

		const uint n_pal = src_img->n_pal;
		if (!n_pal)
			memset (data, 0xff, data_size);
		else
		{
			DASSERT (src_img->pal);
			const u32 *pal = (u32 *)src_img->pal;

			const u16 *src = (u16 *)src_img->data;
			const u16 *src_end = src + src_img->xwidth * src_img->xheight;

			u8 *dest = data;

			while (src < src_end)
			{
				const u16 val = *src++;
				memcpy (dest, pal + (val < n_pal ? val : 0), 4);
				dest += 4;
			}

			DASSERT (dest == data + data_size);
		}

		AssignDataRGB (dest_img, src_img, data);
	}
	else
		ASSERT (0);

	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

enumError ConvertToGRAY (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	palette_format_t pform // wanted palette format => ignored
)
{
	DASSERT (dest_img);
	DASSERT (src_img);

	//--- mipmap support

	if (src_img->mipmap) // --> do the conversion for each image standalone!
	{
		if (src_img != dest_img)
			CopyIMG (dest_img, false, src_img, true); // this makes it easier to handle
		RemoveContainerIMG (dest_img);
		DASSERT (!dest_img->container);

		while (dest_img)
		{
			Image_t *next = dest_img->mipmap;
			dest_img->mipmap = 0;
			enumError err = ConvertToGRAY (dest_img, dest_img, pform);
			dest_img->mipmap = next;
			if (err)
				return err;
			dest_img = next;
		}
		return ERR_OK;
	}

	//--- convert image

	if (src_img->iform == IMG_X_GRAY)
	{
		PRINT ("%*sCONVERT: GRAY {%u,%u} -> GRAY {%u,%u}\n", convert_depth * 2, "",
			src_img->conv_count, src_img->seq_num, dest_img->conv_count, dest_img->seq_num);

		CopyIMG (dest_img, false, src_img, true);
		return ERR_OK;
	}

	if (src_img->iform >= IMG_X_PAL__MIN && src_img->iform <= IMG_X_PAL__MAX)
	{
		convert_depth++;
		enumError err = ConvertToRGB (dest_img, src_img, pform);
		convert_depth--;
		if (err)
			return err;
		src_img = dest_img;
	}

	if (src_img->iform == IMG_X_RGB)
	{
		PRINT ("%*sCONVERT: RGB {%u,%u} -> GRAY {%u,%u}\n", convert_depth * 2, "",
			src_img->conv_count, src_img->seq_num, dest_img->conv_count, dest_img->seq_num);

		DASSERT (EXPAND8 (src_img->width) == src_img->xwidth);
		DASSERT (EXPAND8 (src_img->height) == src_img->xheight);

		const u8 *src = src_img->data;
		const u8 *src_end = src + 4 * src_img->xwidth * src_img->xheight;

		uint data_size;
		u8 *data = AllocDataIMG (src_img, 2, &data_size);
		u8 *dest = data;

		while (src < src_end)
		{
			*dest++ = (src[0] + src[1] + src[2] + 1) / 3;
			src += 3;
			*dest++ = *src++;
		}

		DASSERT (dest == data + data_size);
		AssignDataGRAY (dest_img, src_img, data);
	}
	else
		ASSERT (0);
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

enumError ConvertToPALETTE (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	uint max_pal, // maximum palette values, 0 = auto
	image_format_t iform // if a valid PAL format:
						 //    convert to this and
						 //    reduce 'max_pal' if neccessary
)
{
	DASSERT (dest_img);
	DASSERT (src_img);

	//--- mipmap support

	if (src_img->mipmap) // --> do the conversion for each image standalone!
	{
		if (src_img != dest_img)
			CopyIMG (dest_img, false, src_img, true); // this makes it easier to handle
		RemoveContainerIMG (dest_img);
		DASSERT (!dest_img->container);

		while (dest_img)
		{
			Image_t *next = dest_img->mipmap;
			dest_img->mipmap = 0;
			enumError err = ConvertToPALETTE (dest_img, dest_img, max_pal, iform);
			dest_img->mipmap = next;
			if (err)
				return err;
			dest_img = next;
		}
		return ERR_OK;
	}

	//--- convert image

	bool auto_mode = false;
	uint pal_limit = 0;
	switch (iform)
	{
		case IMG_X_PAL4:
			pal_limit = 0x10;
			break;

		case IMG_X_PAL8:
			pal_limit = 0x100;
			break;

		case IMG_X_PAL14:
			pal_limit = 0x4000;
			break;

		default:
			iform = IMG_X_PAL;
			pal_limit = 0x4000;
			auto_mode = true;
			break;
	}

	if (!max_pal || max_pal > pal_limit)
		max_pal = pal_limit;

	if (src_img->iform >= IMG_X_PAL__MIN && src_img->iform <= IMG_X_PAL__MAX
		&& src_img->n_pal <= max_pal)
	{
		CopyIMG (dest_img, false, src_img, true);
		if (auto_mode)
		{
			if (dest_img->n_pal <= 0x10)
				iform = IMG_X_PAL4;
			else if (dest_img->n_pal <= 0x100)
				iform = IMG_X_PAL8;
			else
				iform = IMG_X_PAL14;
		}
		dest_img->iform = iform;
		return ERR_OK;
	}

	if (src_img->iform != IMG_X_RGB)
	{
		convert_depth++;
		enumError err = ConvertToRGB (dest_img, src_img, PAL_AUTO);
		convert_depth--;
		if (err)
			return err;
		src_img = dest_img;
	}

	PRINT ("%*sCONVERT: RGB {%u,%u} -> PAL {%u,%u}\n", convert_depth * 2, "", src_img->conv_count,
		src_img->seq_num, dest_img->conv_count, dest_img->seq_num);
	if (src_img->iform != IMG_X_RGB)
		ERROR0 (ERR_INTERNAL, 0);

	u16 *index = (u16 *)AllocDataIMG (src_img, 2, 0);
	u8 *pal_data = CALLOC (max_pal, 4);
	uint n_pal = MedianCut ((u32 *)src_img->data, index, src_img->width, src_img->xwidth,
		src_img->height, (u32 *)pal_data, max_pal);

	if (auto_mode)
	{
		if (n_pal <= 0x10)
			iform = IMG_X_PAL4;
		else if (n_pal <= 0x100)
			iform = IMG_X_PAL8;
		else
			iform = IMG_X_PAL14;
	}

	AssignDataPAL (dest_img, src_img, index, pal_data, max_pal * 4, n_pal, iform, 0);
	return ERR_OK;
}

//-----------------------------------------------------------------------------

static enumError conv_to_palette_auto (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	palette_format_t pform // wanted palette format => ignored
)
{
	return ConvertToPALETTE (dest_img, src_img, 0, IMG_X_PAL);
}

//-----------------------------------------------------------------------------

static enumError conv_to_palette_4 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	palette_format_t pform // wanted palette format => ignored
)
{
	return ConvertToPALETTE (dest_img, src_img, 0, IMG_X_PAL8);
}

//-----------------------------------------------------------------------------

static enumError conv_to_palette_8 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	palette_format_t pform // wanted palette format => ignored
)
{
	return ConvertToPALETTE (dest_img, src_img, 0, IMG_X_PAL8);
}

//-----------------------------------------------------------------------------

static enumError conv_to_palette_14 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	palette_format_t pform // wanted palette format => ignored
)
{
	return ConvertToPALETTE (dest_img, src_img, 0, IMG_X_PAL14);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    conv_from_*()		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError conv_from_I4 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	image_format_t iform // new image format, only valid IMG_X_*
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (src_img->iform == IMG_I4);
	DASSERT (iform >= IMG_X__MIN && iform <= IMG_X__MAX);

	const uint bits_per_pixel = 4;
	const uint block_width = 8;
	const uint block_height = 8;

	uint h_blocks, v_blocks, img_size;
	enumError err = CalcImageBlock (
		src_img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, false);
	if (err)
		return err;

	if (iform == IMG_X_RGB)
	{
		// special RGB conversion

		uint data_size;
		u8 *data = AllocDataIMG (src_img, 4, &data_size);
		u8 *dest1 = data;
		const u8 *src = src_img->data;

		const uint block_size = block_width * 4;
		const uint line_size = EXPAND8 (src_img->width) * 4;

		while (v_blocks-- > 0)
		{
			u8 *dest2 = dest1;
			uint hblk = h_blocks;
			while (hblk-- > 0)
			{
				u8 *dest3 = dest2;
				uint iv = block_height;
				while (iv-- > 0)
				{
					uint ih = block_width / 2;
					while (ih-- > 0)
					{
						const u8 val = *src++;
						const u8 rgb1 = cc48[val >> 4];
						*dest3++ = rgb1; // red
						*dest3++ = rgb1; // green
						*dest3++ = rgb1; // blue
						*dest3++ = 0xff; // alpha

						const u8 rgb2 = cc48[val & 0x0f];
						*dest3++ = rgb2; // red
						*dest3++ = rgb2; // green
						*dest3++ = rgb2; // blue
						*dest3++ = 0xff; // alpha
					}
					dest3 += line_size - block_size;
				}
				dest2 += block_size;
			}
			dest1 += line_size * block_height;
		}

		DASSERT (src == src_img->data + img_size);
		DASSERT (dest1 <= data + data_size);
		AssignDataRGB (dest_img, src_img, data);
	}
	else
	{
		// use GRAY conversion elsewise

		uint data_size;
		u8 *data = AllocDataIMG (src_img, 2, &data_size);
		u8 *dest1 = data;
		const u8 *src = src_img->data;

		const uint block_size = block_width * 2;
		const uint line_size = EXPAND8 (src_img->width) * 2;

		while (v_blocks-- > 0)
		{
			u8 *dest2 = dest1;
			uint hblk = h_blocks;
			while (hblk-- > 0)
			{
				u8 *dest3 = dest2;
				uint iv = block_height;
				while (iv-- > 0)
				{
					uint ih = block_width / 2;
					while (ih-- > 0)
					{
						const u8 val = *src++;
						*dest3++ = cc48[val >> 4]; // gray
						*dest3++ = 0xff; // alpha
						*dest3++ = cc48[val & 0x0f]; // gray
						*dest3++ = 0xff; // alpha
					}
					dest3 += line_size - block_size;
				}
				dest2 += block_size;
			}
			dest1 += line_size * block_height;
		}

		DASSERT (src == src_img->data + img_size);
		DASSERT (dest1 <= data + data_size);
		AssignDataGRAY (dest_img, src_img, data);
	}

	dest_img->alpha_status = -1;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError conv_from_I8 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	image_format_t iform // new image format, only valid IMG_X_*
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (src_img->iform == IMG_I8);
	DASSERT (iform >= IMG_X__MIN && iform <= IMG_X__MAX);

	const uint bits_per_pixel = 8;
	const uint block_width = 8;
	const uint block_height = 4;

	uint h_blocks, v_blocks, img_size;
	enumError err = CalcImageBlock (
		src_img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, false);
	if (err)
		return err;

	if (iform == IMG_X_RGB)
	{
		// special RGB conversion

		uint data_size;
		u8 *data = AllocDataIMG (src_img, 4, &data_size);
		u8 *dest1 = data;
		const u8 *src = src_img->data;

		const uint block_size = block_width * 4;
		const uint line_size = EXPAND8 (src_img->width) * 4;

		while (v_blocks-- > 0)
		{
			u8 *dest2 = dest1;
			uint hblk = h_blocks;
			while (hblk-- > 0)
			{
				u8 *dest3 = dest2;
				uint iv = block_height;
				while (iv-- > 0)
				{
					uint ih = block_width;
					while (ih-- > 0)
					{
						const u8 val = *src++;
						*dest3++ = val; // red
						*dest3++ = val; // green
						*dest3++ = val; // blue
						*dest3++ = 0xff; // alpha
					}
					dest3 += line_size - block_size;
				}
				dest2 += block_size;
			}
			dest1 += line_size * block_height;
		}

		DASSERT (src == src_img->data + img_size);
		DASSERT (dest1 <= data + data_size);
		AssignDataRGB (dest_img, src_img, data);
	}
	else
	{
		// use GRAY conversion elsewise

		uint data_size;
		u8 *data = AllocDataIMG (src_img, 2, &data_size);
		u8 *dest1 = data;
		const u8 *src = src_img->data;

		const uint block_size = block_width * 2;
		const uint line_size = EXPAND8 (src_img->width) * 2;

		while (v_blocks-- > 0)
		{
			u8 *dest2 = dest1;
			uint hblk = h_blocks;
			while (hblk-- > 0)
			{
				u8 *dest3 = dest2;
				uint iv = block_height;
				while (iv-- > 0)
				{
					uint ih = block_width;
					while (ih-- > 0)
					{
						*dest3++ = *src++; // gray
						*dest3++ = 0xff; // alpha
					}
					dest3 += line_size - block_size;
				}
				dest2 += block_size;
			}
			dest1 += line_size * block_height;
		}

		DASSERT (src == src_img->data + img_size);
		DASSERT (dest1 <= data + data_size);
		AssignDataGRAY (dest_img, src_img, data);
	}

	dest_img->alpha_status = -1;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError conv_from_IA4 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	image_format_t iform // new image format, only valid IMG_X_*
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (src_img->iform == IMG_IA4);
	DASSERT (iform >= IMG_X__MIN && iform <= IMG_X__MAX);

	const uint bits_per_pixel = 8;
	const uint block_width = 8;
	const uint block_height = 4;

	uint h_blocks, v_blocks, img_size;
	enumError err = CalcImageBlock (
		src_img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, false);
	if (err)
		return err;

	if (iform == IMG_X_RGB)
	{
		// special RGB conversion

		uint data_size;
		u8 *data = AllocDataIMG (src_img, 4, &data_size);
		u8 *dest1 = data;
		const u8 *src = src_img->data;

		const uint block_size = block_width * 4;
		const uint line_size = EXPAND8 (src_img->width) * 4;

		while (v_blocks-- > 0)
		{
			u8 *dest2 = dest1;
			uint hblk = h_blocks;
			while (hblk-- > 0)
			{
				u8 *dest3 = dest2;
				uint iv = block_height;
				while (iv-- > 0)
				{
					uint ih = block_width;
					while (ih-- > 0)
					{
						const u8 val = *src++;
						const u8 rgb = cc48[val & 0x0f];
						*dest3++ = rgb; // red
						*dest3++ = rgb; // green
						*dest3++ = rgb; // blue
						*dest3++ = cc48[val >> 4]; // alpha
					}
					dest3 += line_size - block_size;
				}
				dest2 += block_size;
			}
			dest1 += line_size * block_height;
		}

		DASSERT (src == src_img->data + img_size);
		DASSERT (dest1 <= data + data_size);
		AssignDataRGB (dest_img, src_img, data);
	}
	else
	{
		// use GRAY conversion elsewise

		uint data_size;
		u8 *data = AllocDataIMG (src_img, 2, &data_size);
		u8 *dest1 = data;
		const u8 *src = src_img->data;

		const uint block_size = block_width * 2;
		const uint line_size = EXPAND8 (src_img->width) * 2;

		while (v_blocks-- > 0)
		{
			u8 *dest2 = dest1;
			uint hblk = h_blocks;
			while (hblk-- > 0)
			{
				u8 *dest3 = dest2;
				uint iv = block_height;
				while (iv-- > 0)
				{
					uint ih = block_width;
					while (ih-- > 0)
					{
						const u8 val = *src++;
						*dest3++ = cc48[val & 0x0f]; // gray
						*dest3++ = cc48[val >> 4]; // alpha
					}
					dest3 += line_size - block_size;
				}
				dest2 += block_size;
			}
			dest1 += line_size * block_height;
		}

		DASSERT (src == src_img->data + img_size);
		DASSERT (dest1 <= data + data_size);
		AssignDataGRAY (dest_img, src_img, data);
	}

	dest_img->alpha_status = 0;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError conv_from_IA8 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	image_format_t iform // new image format, only valid IMG_X_*
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (src_img->iform == IMG_IA8);
	DASSERT (iform >= IMG_X__MIN && iform <= IMG_X__MAX);

	const uint bits_per_pixel = 16;
	const uint block_width = 4;
	const uint block_height = 4;

	uint h_blocks, v_blocks, img_size;
	enumError err = CalcImageBlock (
		src_img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, false);
	if (err)
		return err;

	u16 (*rd16) (const void *data_ptr) = src_img->endian->rd16;
	noPRINT ("from_IA8: wh=%u*%u, blk=%u,%u -> nb=%u,%u\n", src_img->width, src_img->height,
		block_width, block_height, h_blocks, v_blocks);

	if (iform == IMG_X_RGB)
	{
		// special RGB conversion

		uint data_size;
		u8 *data = AllocDataIMG (src_img, 4, &data_size);
		u8 *dest1 = data;
		const u8 *src = src_img->data;

		const uint block_size = block_width * 4;
		const uint line_size = EXPAND8 (src_img->width) * 4;

		while (v_blocks-- > 0)
		{
			u8 *dest2 = dest1;
			uint hblk = h_blocks;
			while (hblk-- > 0)
			{
				u8 *dest3 = dest2;
				uint iv = block_height;
				while (iv-- > 0)
				{
					uint ih = block_width;
					while (ih-- > 0)
					{
						const u16 val = rd16 (src);
						src += 2;
						*dest3++ = val; // red
						*dest3++ = val; // green
						*dest3++ = val; // blue
						*dest3++ = val >> 8; // alpha
					}
					dest3 += line_size - block_size;
				}
				dest2 += block_size;
			}
			dest1 += line_size * block_height;
		}

		DASSERT (src == src_img->data + img_size);
		DASSERT (dest1 <= data + data_size);
		AssignDataRGB (dest_img, src_img, data);
	}
	else
	{
		// use GRAY conversion elsewise

		uint data_size;
		u8 *data = AllocDataIMG (src_img, 2, &data_size);
		u8 *dest1 = data;
		const u8 *src = src_img->data;

		const uint block_size = block_width * 2;
		const uint line_size = EXPAND8 (src_img->width) * 2;

		while (v_blocks-- > 0)
		{
			u8 *dest2 = dest1;
			uint hblk = h_blocks;
			while (hblk-- > 0)
			{
				u8 *dest3 = dest2;
				uint iv = block_height;
				while (iv-- > 0)
				{
					uint ih = block_width;
					while (ih-- > 0)
					{
						const u16 val = rd16 (src);
						src += 2;
						*dest3++ = val; // gray
						*dest3++ = val >> 8; // alpha
					}
					dest3 += line_size - block_size;
				}
				dest2 += block_size;
			}
			dest1 += line_size * block_height;
		}

		DASSERT (src == src_img->data + img_size);
		DASSERT (dest1 <= data + data_size);
		AssignDataGRAY (dest_img, src_img, data);
	}

	dest_img->alpha_status = 0;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError conv_from_RGB565 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	image_format_t iform // new image format, only valid IMG_X_*
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (src_img->iform == IMG_RGB565);
	DASSERT (iform >= IMG_X__MIN && iform <= IMG_X__MAX);

	const uint bits_per_pixel = 16;
	const uint block_width = 4;
	const uint block_height = 4;

	uint h_blocks, v_blocks, img_size;
	enumError err = CalcImageBlock (
		src_img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, false);
	if (err)
		return err;

	u16 (*rd16) (const void *data_ptr) = src_img->endian->rd16;

	uint data_size;
	u8 *data = AllocDataIMG (src_img, 4, &data_size);
	u8 *dest1 = data;
	const u8 *src = src_img->data;

	const uint block_size = block_width * 4;
	const uint line_size = EXPAND8 (src_img->width) * 4;

	while (v_blocks-- > 0)
	{
		u8 *dest2 = dest1;
		uint hblk = h_blocks;
		while (hblk-- > 0)
		{
			u8 *dest3 = dest2;
			uint iv = block_height;
			while (iv-- > 0)
			{
				uint ih = block_width;
				while (ih-- > 0)
				{
					const u16 val = rd16 (src);
					src += 2;
					*dest3++ = cc58[val >> 11]; // red
					*dest3++ = cc68[val >> 5 & 0x3f]; // green
					*dest3++ = cc58[val & 0x1f]; // blue
					*dest3++ = 0xff; // alpha
				}
				dest3 += line_size - block_size;
			}
			dest2 += block_size;
		}
		dest1 += line_size * block_height;
	}

	DASSERT (src == src_img->data + img_size);
	DASSERT (dest1 <= data + data_size);
	AssignDataRGB (dest_img, src_img, data);
	dest_img->alpha_status = -1;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError conv_from_RGB5A3 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	image_format_t iform // new image format, only valid IMG_X_*
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (src_img->iform == IMG_RGB5A3);
	DASSERT (iform >= IMG_X__MIN && iform <= IMG_X__MAX);

	const uint bits_per_pixel = 16;
	const uint block_width = 4;
	const uint block_height = 4;

	uint h_blocks, v_blocks, img_size;
	enumError err = CalcImageBlock (
		src_img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, false);
	if (err)
		return err;

	u16 (*rd16) (const void *data_ptr) = src_img->endian->rd16;

	uint data_size;
	u8 *data = AllocDataIMG (src_img, 4, &data_size);
	u8 *dest1 = data;
	const u8 *src = src_img->data;

	const uint block_size = block_width * 4;
	const uint line_size = EXPAND8 (src_img->width) * 4;

	while (v_blocks-- > 0)
	{
		u8 *dest2 = dest1;
		uint hblk = h_blocks;
		while (hblk-- > 0)
		{
			u8 *dest3 = dest2;
			uint iv = block_height;
			while (iv-- > 0)
			{
				uint ih = block_width;
				while (ih-- > 0)
				{
					const u16 val = rd16 (src);
					src += 2;
					if (val & 0x8000)
					{
						*dest3++ = cc58[val >> 10 & 0x1f]; // red
						*dest3++ = cc58[val >> 5 & 0x1f]; // green
						*dest3++ = cc58[val & 0x1f]; // blue
						*dest3++ = 0xff; // alpha
					}
					else
					{
						*dest3++ = cc48[val >> 8 & 0x0f]; // red
						*dest3++ = cc48[val >> 4 & 0x0f]; // green
						*dest3++ = cc48[val & 0x0f]; // blue
						*dest3++ = cc38[val >> 12 & 0x07]; // alpha
					}
				}
				dest3 += line_size - block_size;
			}
			dest2 += block_size;
		}
		dest1 += line_size * block_height;
	}

	DASSERT (src == src_img->data + img_size);
	DASSERT (dest1 <= data + data_size);
	AssignDataRGB (dest_img, src_img, data);
	dest_img->alpha_status = 0;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError conv_from_RGBA32 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	image_format_t iform // new image format, only valid IMG_X_*
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (src_img->iform == IMG_RGBA32);
	DASSERT (iform >= IMG_X__MIN && iform <= IMG_X__MAX);

	const uint bits_per_pixel = 32;
	const uint block_width = 4;
	const uint block_height = 4;

	uint h_blocks, v_blocks, img_size;
	enumError err = CalcImageBlock (
		src_img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, false);
	if (err)
		return err;

	u16 (*rd16) (const void *data_ptr) = src_img->endian->rd16;

	uint data_size;
	u8 *data = AllocDataIMG (src_img, 4, &data_size);
	u8 *dest1 = data;
	const u8 *src = src_img->data;

	const uint block_size = block_width * 4;
	const uint line_size = EXPAND8 (src_img->width) * 4;

	while (v_blocks-- > 0)
	{
		u8 *dest2 = dest1;
		uint hblk = h_blocks;
		while (hblk-- > 0)
		{
			u8 *dest3 = dest2;
			uint iv = block_height;
			while (iv-- > 0)
			{
				uint ih = block_width;
				while (ih-- > 0)
				{
					const u16 ar = rd16 (src);
					const u16 gb = rd16 (src + 0x20);
					src += 2;
					*dest3++ = ar; // red
					*dest3++ = gb >> 8; // green
					*dest3++ = gb; // blue
					*dest3++ = ar >> 8; // alpha
				}
				dest3 += line_size - block_size;
			}
			src += block_height * block_width * 2;
			dest2 += block_size;
		}
		dest1 += line_size * block_height;
	}

	DASSERT (src == src_img->data + img_size);
	DASSERT (dest1 <= data + data_size);
	AssignDataRGB (dest_img, src_img, data);
	dest_img->alpha_status = 0;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError conv_from_C4 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	image_format_t iform // new image format, only valid IMG_X_*
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (src_img->iform == IMG_C4);
	DASSERT (iform >= IMG_X__MIN && iform <= IMG_X__MAX);

	const uint bits_per_pixel = 4;
	const uint block_width = 8;
	const uint block_height = 8;

	uint h_blocks, v_blocks, img_size;
	enumError err = CalcImageBlock (
		src_img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, false);
	if (err)
		return err;

	// here we transform the data to a linear 16-bit palette-index table

	const uint xwidth = EXPAND8 (src_img->width);
	const uint xheight = EXPAND8 (src_img->height);
	u16 *itab = CALLOC (xwidth * xheight, 2);
	u16 *dest1 = itab;
	const u8 *src = src_img->data;

	while (v_blocks-- > 0)
	{
		u16 *dest2 = dest1;
		uint hblk = h_blocks;
		while (hblk-- > 0)
		{
			u16 *dest3 = dest2;
			uint iv = block_height;
			while (iv-- > 0)
			{
				uint ih = block_width / 2;
				while (ih-- > 0)
				{
					const u8 val = *src++;
					*dest3++ = val >> 4;
					*dest3++ = val & 0x0f;
				}
				dest3 += xwidth - block_width;
			}
			dest2 += block_width;
		}
		dest1 += xwidth * block_height;
	}
	noPRINT ("src=0x%zx/0x%x = %zu/%u [%u*%u]\n", src - src_img->data, img_size,
		src - src_img->data, img_size, src_img->width, src_img->height);
	DASSERT (src == src_img->data + img_size);
	DASSERT (dest1 <= itab + xwidth * xheight * 2);

	//--- now do the palette transformation

	return TransformPalette (dest_img, src_img, IMG_X_PAL4, itab);
}

///////////////////////////////////////////////////////////////////////////////

static enumError conv_from_C8 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	image_format_t iform // new image format, only valid IMG_X_*
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (src_img->iform == IMG_C8);
	DASSERT (iform >= IMG_X__MIN && iform <= IMG_X__MAX);

	const uint bits_per_pixel = 8;
	const uint block_width = 8;
	const uint block_height = 4;

	uint h_blocks, v_blocks, img_size;
	enumError err = CalcImageBlock (
		src_img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, false);
	if (err)
		return err;

	// here we transform the data to a linear 16-bit palette-index table

	const uint xwidth = EXPAND8 (src_img->width);
	const uint xheight = EXPAND8 (src_img->height);
	u16 *itab = CALLOC (xwidth * xheight, 2);
	u16 *dest1 = itab;
	const u8 *src = src_img->data;

	while (v_blocks-- > 0)
	{
		u16 *dest2 = dest1;
		uint hblk = h_blocks;
		while (hblk-- > 0)
		{
			u16 *dest3 = dest2;
			uint iv = block_height;
			while (iv-- > 0)
			{
				uint ih = block_width;
				while (ih-- > 0)
					*dest3++ = *src++;
				dest3 += xwidth - block_width;
			}
			dest2 += block_width;
		}
		dest1 += xwidth * block_height;
	}
	noPRINT ("src=0x%zx/0x%x = %zu/%u [%u*%u]\n", src - src_img->data, img_size,
		src - src_img->data, img_size, src_img->width, src_img->height);
	DASSERT (src == src_img->data + img_size);
	DASSERT (dest1 <= itab + xwidth * xheight * 2);

	//--- now do the palette transformation

	return TransformPalette (dest_img, src_img, IMG_X_PAL8, itab);
}

///////////////////////////////////////////////////////////////////////////////

static enumError conv_from_C14X2 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	image_format_t iform // new image format, only valid IMG_X_*
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (src_img->iform == IMG_C14X2);
	DASSERT (iform >= IMG_X__MIN && iform <= IMG_X__MAX);

	const uint bits_per_pixel = 16;
	const uint block_width = 4;
	const uint block_height = 4;

	uint h_blocks, v_blocks, img_size;
	enumError err = CalcImageBlock (
		src_img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, false);
	if (err)
		return err;

	// here we transform the data to a linear 16-bit palette-index table

	u16 (*rd16) (const void *data_ptr) = src_img->endian->rd16;

	const uint xwidth = EXPAND8 (src_img->width);
	const uint xheight = EXPAND8 (src_img->height);
	u16 *itab = CALLOC (xwidth * xheight, 2);
	u16 *dest1 = itab;
	const u8 *src = src_img->data;

	while (v_blocks-- > 0)
	{
		u16 *dest2 = dest1;
		uint hblk = h_blocks;
		while (hblk-- > 0)
		{
			u16 *dest3 = dest2;
			uint iv = block_height;
			while (iv-- > 0)
			{
				uint ih = block_width;
				while (ih-- > 0)
				{
					*dest3++ = rd16 (src) & 0x3fff;
					src += 2;
				}
				dest3 += xwidth - block_width;
			}
			dest2 += block_width;
		}
		dest1 += xwidth * block_height;
	}
	noPRINT ("src=0x%zx/0x%x = %zu/%u [%u*%u]\n", src - src_img->data, img_size,
		src - src_img->data, img_size, src_img->width, src_img->height);
	DASSERT (src == src_img->data + img_size);
	DASSERT (dest1 <= itab + xwidth * xheight * 2);

	//--- now do the palette transformation

	return TransformPalette (dest_img, src_img, IMG_X_PAL14, itab);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    conv_to_*()			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError conv_to_I4 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	palette_format_t pform // wanted palette format => ignored
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (src_img->iform >= IMG_X__MIN && src_img->iform <= IMG_X__MAX);

	//--- first convert to IMG_X_GRAY

	if (src_img->iform != IMG_X_GRAY)
	{
		enumError err = ConvertIMG (dest_img, false, src_img, IMG_X_GRAY, PAL_INVALID);
		if (err)
			return err;
		src_img = dest_img;
	}
	DASSERT (src_img->iform == IMG_X_GRAY);

	//--- and now convert IMG_X_GRAY -> I4

	const uint bits_per_pixel = 4;
	const uint block_width = 8;
	const uint block_height = 8;

	uint h_blocks, v_blocks, img_size;
	CalcImageBlock (
		src_img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, true);

	// use GRAY conversion elsewise

	u8 *data = CALLOC (1, img_size);
	u8 *dest = data;
	const u8 *src1 = src_img->data;

	const uint block_size = block_width * 2;
	DASSERT (EXPAND8 (src_img->width) == src_img->xwidth);
	const uint line_size = src_img->xwidth * 2;

	while (v_blocks-- > 0)
	{
		const u8 *src2 = src1;
		uint hblk = h_blocks;
		while (hblk-- > 0)
		{
			const u8 *src3 = src2;
			uint iv = block_height;
			while (iv-- > 0)
			{
				uint ih = block_width / 2;
				while (ih-- > 0)
				{
					*dest++ = cc84[src3[0]] << 4 | cc84[src3[2]];
					src3 += 4;
				}
				src3 += line_size - block_size;
			}
			src2 += block_size;
		}
		src1 += line_size * block_height;
	}

	DASSERT (dest == data + img_size);
	DASSERT (src1 <= src_img->data + src_img->data_size);

	AssignData (dest_img, src_img, data, img_size, IMG_I4);
	dest_img->is_grayed = true;
	dest_img->alpha_status = -1;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError conv_to_I8 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	palette_format_t pform // wanted palette format => ignored
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (src_img->iform >= IMG_X__MIN && src_img->iform <= IMG_X__MAX);

	//--- first convert to IMG_X_GRAY

	if (src_img->iform != IMG_X_GRAY)
	{
		enumError err = ConvertIMG (dest_img, false, src_img, IMG_X_GRAY, PAL_INVALID);
		if (err)
			return err;
		src_img = dest_img;
	}
	DASSERT (src_img->iform == IMG_X_GRAY);

	//--- and now convert IMG_X_GRAY -> I8

	const uint bits_per_pixel = 8;
	const uint block_width = 8;
	const uint block_height = 4;

	uint h_blocks, v_blocks, img_size;
	CalcImageBlock (
		src_img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, true);

	u8 *data = CALLOC (1, img_size);
	u8 *dest = data;
	const u8 *src1 = src_img->data;

	const uint block_size = block_width * 2;
	DASSERT (EXPAND8 (src_img->width) == src_img->xwidth);
	const uint line_size = src_img->xwidth * 2;

	while (v_blocks-- > 0)
	{
		const u8 *src2 = src1;
		uint hblk = h_blocks;
		while (hblk-- > 0)
		{
			const u8 *src3 = src2;
			uint iv = block_height;
			while (iv-- > 0)
			{
				uint ih = block_width;
				while (ih-- > 0)
				{
					*dest++ = *src3; // gray
					src3 += 2; // skip alpha
				}
				src3 += line_size - block_size;
			}
			src2 += block_size;
		}
		src1 += line_size * block_height;
	}

	DASSERT (dest == data + img_size);
	DASSERT (src1 <= src_img->data + src_img->data_size);

	AssignData (dest_img, src_img, data, img_size, IMG_I8);
	dest_img->is_grayed = true;
	dest_img->alpha_status = -1;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError conv_to_IA4 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	palette_format_t pform // wanted palette format => ignored
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (src_img->iform >= IMG_X__MIN && src_img->iform <= IMG_X__MAX);

	//--- first convert to IMG_X_GRAY

	if (src_img->iform != IMG_X_GRAY)
	{
		enumError err = ConvertIMG (dest_img, false, src_img, IMG_X_GRAY, PAL_INVALID);
		if (err)
			return err;
		src_img = dest_img;
	}
	DASSERT (src_img->iform == IMG_X_GRAY);

	//--- and now convert IMG_X_GRAY -> IA4

	const uint bits_per_pixel = 8;
	const uint block_width = 8;
	const uint block_height = 4;

	uint h_blocks, v_blocks, img_size;
	CalcImageBlock (
		src_img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, true);

	u8 *data = CALLOC (1, img_size);
	u8 *dest = data;
	const u8 *src1 = src_img->data;

	const uint block_size = block_width * 2;
	DASSERT (EXPAND8 (src_img->width) == src_img->xwidth);
	const uint line_size = src_img->xwidth * 2;

	while (v_blocks-- > 0)
	{
		const u8 *src2 = src1;
		uint hblk = h_blocks;
		while (hblk-- > 0)
		{
			const u8 *src3 = src2;
			uint iv = block_height;
			while (iv-- > 0)
			{
				uint ih = block_width;
				while (ih-- > 0)
				{
					*dest++ = cc84[src3[1]] << 4 | cc84[src3[0]];
					src3 += 2;
				}
				src3 += line_size - block_size;
			}
			src2 += block_size;
		}
		src1 += line_size * block_height;
	}

	DASSERT (dest == data + img_size);
	DASSERT (src1 <= src_img->data + src_img->data_size);

	AssignData (dest_img, src_img, data, img_size, IMG_IA4);
	dest_img->is_grayed = true;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError conv_to_IA8 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	palette_format_t pform // wanted palette format => ignored
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (src_img->iform >= IMG_X__MIN && src_img->iform <= IMG_X__MAX);

	//--- first convert to IMG_X_GRAY

	if (src_img->iform != IMG_X_GRAY)
	{
		enumError err = ConvertIMG (dest_img, false, src_img, IMG_X_GRAY, PAL_INVALID);
		if (err)
			return err;
		src_img = dest_img;
	}
	DASSERT (src_img->iform == IMG_X_GRAY);

	//--- and now convert IMG_X_GRAY -> IA8

	const uint bits_per_pixel = 16;
	const uint block_width = 4;
	const uint block_height = 4;

	uint h_blocks, v_blocks, img_size;
	CalcImageBlock (
		src_img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, true);

	void (*wr16) (void *, u16) = src_img->endian->wr16;

	u8 *data = CALLOC (1, img_size);
	u8 *dest = data;
	const u8 *src1 = src_img->data;

	const uint block_size = block_width * 2;
	DASSERT (EXPAND8 (src_img->width) == src_img->xwidth);
	const uint line_size = src_img->xwidth * 2;

	while (v_blocks-- > 0)
	{
		const u8 *src2 = src1;
		uint hblk = h_blocks;
		while (hblk-- > 0)
		{
			const u8 *src3 = src2;
			uint iv = block_height;
			while (iv-- > 0)
			{
				uint ih = block_width;
				while (ih-- > 0)
				{
					wr16 (dest, src3[0] | (u16)src3[1] << 8);
					dest += 2;
					src3 += 2;
				}
				src3 += line_size - block_size;
			}
			src2 += block_size;
		}
		src1 += line_size * block_height;
	}

	DASSERT (dest == data + img_size);
	DASSERT (src1 <= src_img->data + src_img->data_size);

	AssignData (dest_img, src_img, data, img_size, IMG_IA8);
	dest_img->is_grayed = true;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError conv_to_RGB565 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	palette_format_t pform // wanted palette format => ignored
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (src_img->iform >= IMG_X__MIN && src_img->iform <= IMG_X__MAX);

	//--- first convert to IMG_X_RGB

	if (src_img->iform != IMG_X_RGB)
	{
		enumError err = ConvertIMG (dest_img, false, src_img, IMG_X_RGB, PAL_INVALID);
		if (err)
			return err;
		src_img = dest_img;
	}
	DASSERT (src_img->iform == IMG_X_RGB);

	//--- and now convert IMG_X_RGB -> RGB565

	const uint bits_per_pixel = 16;
	const uint block_width = 4;
	const uint block_height = 4;

	uint h_blocks, v_blocks, img_size;
	CalcImageBlock (
		src_img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, true);

	void (*wr16) (void *, u16) = src_img->endian->wr16;

	u8 *data = CALLOC (1, img_size);
	u8 *dest = data;
	const u8 *src1 = src_img->data;

	const uint block_size = block_width * 4;
	DASSERT (EXPAND8 (src_img->width) == src_img->xwidth);
	const uint line_size = src_img->xwidth * 4;

	while (v_blocks-- > 0)
	{
		const u8 *src2 = src1;
		uint hblk = h_blocks;
		while (hblk-- > 0)
		{
			const u8 *src3 = src2;
			uint iv = block_height;
			while (iv-- > 0)
			{
				uint ih = block_width;
				while (ih-- > 0)
				{
					wr16 (dest, cc85[src3[0]] << 11 | cc86[src3[1]] << 5 | cc85[src3[2]]);
					dest += 2;
					src3 += 4;
				}
				src3 += line_size - block_size;
			}
			src2 += block_size;
		}
		src1 += line_size * block_height;
	}

	DASSERT (dest == data + img_size);
	DASSERT (src1 <= src_img->data + src_img->data_size);

	AssignData (dest_img, src_img, data, img_size, IMG_RGB565);
	dest_img->alpha_status = -1;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError conv_to_RGB5A3 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	palette_format_t pform // wanted palette format => ignored
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (src_img->iform >= IMG_X__MIN && src_img->iform <= IMG_X__MAX);

	//--- first convert to IMG_X_RGB

	if (src_img->iform != IMG_X_RGB)
	{
		enumError err = ConvertIMG (dest_img, false, src_img, IMG_X_RGB, PAL_INVALID);
		if (err)
			return err;
		src_img = dest_img;
	}
	DASSERT (src_img->iform == IMG_X_RGB);

	//--- and now convert IMG_X_RGB -> RGB5A3

	const uint bits_per_pixel = 16;
	const uint block_width = 4;
	const uint block_height = 4;

	uint h_blocks, v_blocks, img_size;
	CalcImageBlock (
		src_img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, true);

	void (*wr16) (void *, u16) = src_img->endian->wr16;

	u8 *data = CALLOC (1, img_size);
	u8 *dest = data;
	const u8 *src1 = src_img->data;

	const uint block_size = block_width * 4;
	DASSERT (EXPAND8 (src_img->width) == src_img->xwidth);
	const uint line_size = src_img->xwidth * 4;

	while (v_blocks-- > 0)
	{
		const u8 *src2 = src1;
		uint hblk = h_blocks;
		while (hblk-- > 0)
		{
			const u8 *src3 = src2;
			uint iv = block_height;
			while (iv-- > 0)
			{
				uint ih = block_width;
				while (ih-- > 0)
				{
					if (src3[3] == 0xff)
						wr16 (dest,
							cc85[src3[0]] << 10 | cc85[src3[1]] << 5 | cc85[src3[2]] | 0x8000);
					else
						wr16 (dest,
							cc84[src3[0]] << 8 | cc84[src3[1]] << 4 | cc84[src3[2]]
								| cc83[src3[3]] << 12);
					dest += 2;
					src3 += 4;
				}
				src3 += line_size - block_size;
			}
			src2 += block_size;
		}
		src1 += line_size * block_height;
	}

	DASSERT (dest == data + img_size);
	DASSERT (src1 <= src_img->data + src_img->data_size);

	AssignData (dest_img, src_img, data, img_size, IMG_RGB5A3);
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError conv_to_RGBA32 (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	palette_format_t pform // wanted palette format => ignored
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (src_img->iform >= IMG_X__MIN && src_img->iform <= IMG_X__MAX);

	//--- first convert to IMG_X_RGB

	if (src_img->iform != IMG_X_RGB)
	{
		enumError err = ConvertIMG (dest_img, false, src_img, IMG_X_RGB, PAL_INVALID);
		if (err)
			return err;
		src_img = dest_img;
	}
	DASSERT (src_img->iform == IMG_X_RGB);

	//--- and now convert IMG_X_RGB -> RGBA32

	const uint bits_per_pixel = 32;
	const uint block_width = 4;
	const uint block_height = 4;

	uint h_blocks, v_blocks, img_size;
	CalcImageBlock (
		src_img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, true);

	void (*wr16) (void *, u16) = src_img->endian->wr16;

	u8 *data = CALLOC (1, img_size);
	u8 *dest = data;
	const u8 *src1 = src_img->data;

	const uint block_size = block_width * 4;
	DASSERT (EXPAND8 (src_img->width) == src_img->xwidth);
	const uint line_size = src_img->xwidth * 4;

	while (v_blocks-- > 0)
	{
		const u8 *src2 = src1;
		uint hblk = h_blocks;
		while (hblk-- > 0)
		{
			const u8 *src3 = src2;
			uint iv = block_height;
			while (iv-- > 0)
			{
				uint ih = block_width;
				while (ih-- > 0)
				{
					wr16 (dest, src3[0] | (u16)src3[3] << 8);
					wr16 (dest + 0x20, src3[2] | (u16)src3[1] << 8);
					dest += 2;
					src3 += 4;
				}
				src3 += line_size - block_size;
			}
			dest += block_height * block_width * 2;
			src2 += block_size;
		}
		src1 += line_size * block_height;
	}

	DASSERT (dest == data + img_size);
	DASSERT (src1 <= src_img->data + src_img->data_size);

	AssignData (dest_img, src_img, data, img_size, IMG_RGBA32);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    conv_to_C*()		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError create_C_palette (Image_t *img, // valid destination
	const Image_t *src_img, // valid source
	image_format_t iform, // wanted image format (one of IMG_X_PAL*)
	palette_format_t pform // wanted palette format
)
{
	DASSERT (img);

	//--- convert to IMG_X_RGB

	CopyIMG (img, false, src_img, true);
	Transform2XRGB (img);
	enumError err = ExecTransformIMG (img);
	if (err)
		return err;
	DASSERT (img->iform == IMG_X_RGB);

	//--- find palette format

	switch (pform)
	{
		case PAL_IA8:
		case PAL_RGB565:
		case PAL_RGB5A3:
			// ok for the moment
			break;

		default:
			// calculate the best palette type
			pform = img->is_grayed				 ? PAL_IA8
				: CheckAlphaIMG (img, false) < 0 ? PAL_RGB565
												 : PAL_RGB5A3;
			break;
	}

	PRINT ("%*s  create_C_palette() %s.%s\n", 2 * convert_depth, "",
		GetImageFormatName (iform, "?"), GetPaletteFormatName (pform, "?"));

	uint target_colors = 256;
	if (iform == IMG_X_PAL4)
		target_colors = 16;
	else if (iform == IMG_X_PAL8)
		target_colors = 256;
	else if (iform == IMG_X_PAL14)
		target_colors = 16384;

	u8 *raw_pal = 0;
	uint pal_size = 0, n_colors = 0;
	u16 *indices = 0;
	err = QuantizePalette_PLT0 (img->data, img->width, img->height, img->xwidth, pform,
		target_colors, &raw_pal, &pal_size, &n_colors, &indices);
	if (err)
		return err;

	if (img->data && img->data_alloced)
		FREE (img->data);
	img->data = (u8 *)indices;
	img->data_size = img->xwidth * img->height * 2;
	img->data_alloced = true;

	if (img->pal && img->pal_alloced)
		FREE (img->pal);
	img->pal = raw_pal;
	img->pal_size = pal_size;
	img->n_pal = n_colors;
	img->pal_alloced = true;
	img->pform = pform;
	img->iform = iform;

	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

static enumError conv_to_C4 (Image_t *img, // valid destination
	const Image_t *src_img, // valid source
	palette_format_t pform // wanted palette format
)
{
	DASSERT (img);
	DASSERT (src_img);
	PRINT ("conv_to_C8()\n");

	enumError err = create_C_palette (img, src_img, IMG_X_PAL4, pform);
	if (err)
		return err;

	const uint bits_per_pixel = 4;
	const uint block_width = 8;
	const uint block_height = 8;

	uint h_blocks, v_blocks, img_size;
	CalcImageBlock (
		img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, true);

	const uint xwidth = EXPAND8 (img->width);
	// const uint xheight = EXPAND8(img->height);
	u8 *data = CALLOC (1, img_size);
	u8 *dest = data;
	const u16 *src1 = (u16 *)img->data;

	while (v_blocks-- > 0)
	{
		const u16 *src2 = src1;
		uint hblk = h_blocks;
		while (hblk-- > 0)
		{
			const u16 *src3 = src2;
			uint iv = block_height;
			while (iv-- > 0)
			{
				uint ih = block_width / 2;
				while (ih-- > 0)
				{
					*dest++ = src3[0] << 4 | src3[1];
					src3 += 2;
				}
				src3 += xwidth - block_width;
			}
			src2 += block_width;
		}
		src1 += xwidth * block_height;
	}

	DASSERT (dest == data + img_size);
	DASSERT ((u8 *)src1 <= img->data + img->data_size);

	img->data = data;
	img->data_alloced = true;
	img->data_size = img_size;
	img->xwidth = EXPAND8 (img->width);
	img->xheight = EXPAND8 (img->height);
	img->iform = IMG_C4;
	img->alpha_status = 0;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError conv_to_C8 (Image_t *img, // valid destination
	const Image_t *src_img, // valid source
	palette_format_t pform // wanted palette format
)
{
	DASSERT (img);
	DASSERT (src_img);
	PRINT ("conv_to_C8()\n");

	enumError err = create_C_palette (img, src_img, IMG_X_PAL8, pform);
	if (err)
		return err;

	const uint bits_per_pixel = 8;
	const uint block_width = 8;
	const uint block_height = 4;

	uint h_blocks, v_blocks, img_size;
	CalcImageBlock (
		img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, true);

	const uint xwidth = EXPAND8 (img->width);
	// const uint xheight = EXPAND8(img->height);
	u8 *data = CALLOC (1, img_size);
	u8 *dest = data;
	const u16 *src1 = (u16 *)img->data;

	while (v_blocks-- > 0)
	{
		const u16 *src2 = src1;
		uint hblk = h_blocks;
		while (hblk-- > 0)
		{
			const u16 *src3 = src2;
			uint iv = block_height;
			while (iv-- > 0)
			{
				uint ih = block_width;
				while (ih-- > 0)
					*dest++ = *src3++;
				src3 += xwidth - block_width;
			}
			src2 += block_width;
		}
		src1 += xwidth * block_height;
	}

	DASSERT (dest == data + img_size);
	DASSERT ((u8 *)src1 <= img->data + img->data_size);

	img->data = data;
	img->data_alloced = true;
	img->data_size = img_size;
	img->xwidth = EXPAND8 (img->width);
	img->xheight = EXPAND8 (img->height);
	img->iform = IMG_C8;
	img->alpha_status = 0;
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError conv_to_C14X2 (Image_t *img, // valid destination
	const Image_t *src_img, // valid source
	palette_format_t pform // wanted palette format
)
{
	DASSERT (img);
	DASSERT (src_img);
	PRINT ("conv_to_C14X2()\n");

	enumError err = create_C_palette (img, src_img, IMG_X_PAL14, pform);
	if (err)
		return err;

	const uint bits_per_pixel = 16;
	const uint block_width = 4;
	const uint block_height = 4;

	uint h_blocks, v_blocks, img_size;
	CalcImageBlock (
		img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, true);

	const uint xwidth = EXPAND8 (img->width);
	// const uint xheight = EXPAND8(img->height);
	u8 *data = CALLOC (1, img_size);
	u8 *dest = data;
	const u16 *src1 = (u16 *)img->data;
	void (*wr16) (void *, u16) = img->endian->wr16;

	while (v_blocks-- > 0)
	{
		const u16 *src2 = src1;
		uint hblk = h_blocks;
		while (hblk-- > 0)
		{
			const u16 *src3 = src2;
			uint iv = block_height;
			while (iv-- > 0)
			{
				uint ih = block_width;
				while (ih-- > 0)
				{
					wr16 (dest, *src3++);
					dest += 2;
				}
				src3 += xwidth - block_width;
			}
			src2 += block_width;
		}
		src1 += xwidth * block_height;
	}

	DASSERT (dest == data + img_size);
	DASSERT ((u8 *)src1 <= img->data + img->data_size);

	img->data = data;
	img->data_alloced = true;
	img->data_size = img_size;
	img->xwidth = EXPAND8 (img->width);
	img->xheight = EXPAND8 (img->height);
	img->iform = IMG_C14X2;
	img->alpha_status = 0;
	return ERR_OK;
}

//

//
///////////////////////////////////////////////////////////////////////////////
///////////////			IMG conversions			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError ConvertIMG (Image_t *dest, // destination of conversion
	bool init_dest, // true: initialize 'dest' first
	const Image_t *src, // source of conversion.
						// if src==NULL or src==dest
						//   -> inplace conversion of 'dest'
	image_format_t iform, // new image format
	palette_format_t pform // new palette format
)
{
	DASSERT (dest);
	if (init_dest)
		InitializeIMG (dest);
	if (!src)
		src = dest;

	//--- base setup

	image_format_t temp_iform;
	iform = NormalizeIF (&temp_iform, iform, src->iform, pform);
	pform = NormalizePF (iform, pform, src->pform);
	PRINT ("%*sCONVERT-IMG: [ %-6s -> %-6s -> %-6s ] [%s->%s] mm=%u {%u,%u}\n", convert_depth * 2,
		"", GetImageFormatName (src->iform, "?"), GetImageFormatName (temp_iform, "?"),
		GetImageFormatName (iform, "?"), GetPaletteFormatName (src->pform, "?"),
		GetPaletteFormatName (pform, "?"), CountMipmapsIMG (src), src->conv_count, src->seq_num);

	convert_depth++;

	//--- mipmap support

	if (src->mipmap) // --> do the conversion for each image standalone!
	{
		if (src != dest)
			CopyIMG (dest, false, src, true); // this makes it easier to handle
		RemoveContainerIMG (dest);
		DASSERT (!dest->container);

		while (dest)
		{
			Image_t *next = dest->mipmap;
			dest->mipmap = 0;
			enumError err = ConvertIMG (dest, false, dest, iform, pform);
			dest->mipmap = next;
			if (err)
			{
				convert_depth--;
				return err;
			}
			dest = next;
		}
		convert_depth--;
		return ERR_OK;
	}

	//--- some more tests

	if (convert_depth > 10 || src->iform == IMG_INVALID
		|| iform == src->iform && (src->pform == PAL_INVALID || pform == src->pform))
	{
		convert_depth--;
		CopyIMG (dest, false, src, true);
		return ERR_OK;
	}

	//--- select sub conversions I

	enumError (*func1) (Image_t *, const Image_t *, image_format_t) = 0;

	switch (src->iform)
	{
		case IMG_I4:
			func1 = conv_from_I4;
			break;
		case IMG_I8:
			func1 = conv_from_I8;
			break;
		case IMG_IA4:
			func1 = conv_from_IA4;
			break;
		case IMG_IA8:
			func1 = conv_from_IA8;
			break;
		case IMG_RGB565:
			func1 = conv_from_RGB565;
			break;
		case IMG_RGB5A3:
			func1 = conv_from_RGB5A3;
			break;
		case IMG_RGBA32:
			func1 = conv_from_RGBA32;
			break;
		case IMG_C4:
			func1 = conv_from_C4;
			break;
		case IMG_C8:
			func1 = conv_from_C8;
			break;
		case IMG_C14X2:
			func1 = conv_from_C14X2;
			break;
		case IMG_CMPR:
			func1 = conv_from_CMPR;
			break;

		case IMG_X_GRAY:
			break;
		case IMG_X_RGB:
			break;
		case IMG_X_PAL4:
			break;
		case IMG_X_PAL8:
			break;
		case IMG_X_PAL14:
			break;

		default:
			convert_depth--;
			return ERROR0 (ERR_INVALID_IFORM, "Image format 0x%02x [%s] not supported: %s\n",
				src->iform, GetImageFormatName (src->iform, "?"), src->path);
	}

	if (func1)
	{
		const enumError err = func1 (dest, src, temp_iform);
		if (err)
		{
			convert_depth--;
			return err;
		}
		src = dest;
	}

	//--- select sub conversions II

	if (temp_iform != src->iform)
	{
		enumError (*func2) (Image_t *, const Image_t *, palette_format_t) = 0;
		switch (temp_iform)
		{
			case IMG_X_GRAY:
				func2 = ConvertToGRAY;
				break;
			case IMG_X_RGB:
				func2 = ConvertToRGB;
				break;
			case IMG_X_PAL:
				func2 = conv_to_palette_auto;
				break;
			case IMG_X_PAL4:
				func2 = conv_to_palette_4;
				break;
			case IMG_X_PAL8:
				func2 = conv_to_palette_8;
				break;
			case IMG_X_PAL14:
				func2 = conv_to_palette_14;
				break;

			default:
				return ERROR0 (ERR_INTERNAL, 0);
		}

		if (func2)
		{
			const enumError err = func2 (dest, src, pform);
			if (err)
			{
				convert_depth--;
				return err;
			}
			src = dest;
		}
	}

	//--- select sub conversions III

	enumError (*func2) (Image_t *, const Image_t *, palette_format_t) = 0;
	switch (iform)
	{
		case IMG_I4:
			func2 = conv_to_I4;
			break;
		case IMG_I8:
			func2 = conv_to_I8;
			break;
		case IMG_IA4:
			func2 = conv_to_IA4;
			break;
		case IMG_IA8:
			func2 = conv_to_IA8;
			break;
		case IMG_RGB565:
			func2 = conv_to_RGB565;
			break;
		case IMG_RGB5A3:
			func2 = conv_to_RGB5A3;
			break;
		case IMG_RGBA32:
			func2 = conv_to_RGBA32;
			break;
		case IMG_CMPR:
			func2 = conv_to_CMPR;
			break;
		case IMG_X_GRAY:
			func2 = ConvertToGRAY;
			break;
		case IMG_X_RGB:
			func2 = ConvertToRGB;
			break;
		case IMG_X_PAL:
			func2 = conv_to_palette_auto;
			break;
		case IMG_X_PAL4:
			func2 = conv_to_palette_4;
			break;
		case IMG_X_PAL8:
			func2 = conv_to_palette_8;
			break;
		case IMG_X_PAL14:
			func2 = conv_to_palette_14;
			break;

		case IMG_C4:
			func2 = conv_to_C4;
			break;
		case IMG_C8:
			func2 = conv_to_C8;
			break;
		case IMG_C14X2:
			func2 = conv_to_C14X2;
			break;

		default:
			convert_depth--;
			return ERROR0 (ERR_INVALID_IFORM,
				"Conversion to image format 0x%02x [%s] not supported: %s\n", iform,
				GetImageFormatName (iform, "?"), src->path);
	}

	enumError err = func2 ? func2 (dest, src, pform) : ERR_OK;
	convert_depth--;
	return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			Normalize IF + PF		///////////////
///////////////////////////////////////////////////////////////////////////////

image_format_t NormalizeIF (image_format_t *xform, // not NULL: store the best IMG_X_* here
	image_format_t iform, // wanted image format
	image_format_t x_default, // default format, use if iform == AUTO
							  // and return a X-format
	palette_format_t pform // related palette format
)
{
	switch (iform)
	{
		case IMG_I4: // known gray formats
		case IMG_I8:
		case IMG_IA4:
		case IMG_IA8:
		case IMG_X_GRAY:
			if (xform)
				*xform = IMG_X_GRAY;
			return iform;

		case IMG_RGB565: // known RGB formats
		case IMG_RGB5A3:
		case IMG_RGBA32:
		case IMG_CMPR:
		case IMG_X_RGB:
		case IMG_C4:
		case IMG_C8:
		case IMG_C14X2:
			if (xform)
				*xform = IMG_X_RGB;
			return iform;

		case IMG_X_PAL4:
		case IMG_X_PAL8:
		case IMG_X_PAL14:
			if (xform)
				*xform = iform;
			return iform;

		case IMG_X_PAL:
			switch (x_default)
			{
				case IMG_I4:
					if (xform)
						*xform = IMG_X_PAL4;
					return IMG_X_PAL4;

				case IMG_I8:
				case IMG_IA4:
					if (xform)
						*xform = IMG_X_PAL8;
					return IMG_X_PAL8;

				default:
					if (xform)
						*xform = IMG_X_PAL14;
					return IMG_X_PAL14;
			}

		case IMG_X_AUTO:
			switch (x_default)
			{
				case IMG_I4:
				case IMG_I8:
				case IMG_IA4:
				case IMG_IA8:
				case IMG_X_GRAY:
					if (xform)
						*xform = IMG_X_GRAY;
					return IMG_X_GRAY;

				case IMG_C4:
				case IMG_X_PAL4:
					if (xform)
						*xform = IMG_X_PAL4;
					return x_default;

				case IMG_C8:
				case IMG_X_PAL8:
					if (xform)
						*xform = IMG_X_PAL8;
					return x_default;

				case IMG_C14X2:
				case IMG_X_PAL14:
					if (xform)
						*xform = IMG_X_PAL14;
					return x_default;

				default:
					if (xform)
						*xform = IMG_X_RGB;
					return IMG_X_RGB;
			}

		default:
			return NormalizeIF (xform, x_default, IMG_X_RGB, pform);
	}
}

///////////////////////////////////////////////////////////////////////////////

palette_format_t NormalizePF (image_format_t iform, // related image format
	palette_format_t pform, // wanted palette format
	palette_format_t default_pform // palette format for auto
)
{
	noPRINT ("NormalizePF(%s,%s,%s)\n", GetImageFormatName (iform, "?"),
		GetPaletteFormatName (pform, "?"), GetPaletteFormatName (default_pform, "?"));

	switch (iform)
	{
		case IMG_C4:
		case IMG_C8:
		case IMG_C14X2:
			switch (pform)
			{
				case PAL_IA8:
				case PAL_RGB565:
				case PAL_RGB5A3:
					return pform;

				default:
					switch (default_pform)
					{
						case PAL_IA8:
						case PAL_RGB565:
						case PAL_RGB5A3:
							return default_pform;

						case PAL_AUTO:
							return PAL_DEFAULT;

						default:
							break;
					}
					break;
			}
			break;

		case IMG_X_PAL4:
		case IMG_X_PAL8:
		case IMG_X_PAL14:
		case IMG_X_PAL:
			return PAL_X_RGB;

		default:
			break;
	}

	return PAL_INVALID;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    END				///////////////
///////////////////////////////////////////////////////////////////////////////
