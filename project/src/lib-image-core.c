
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
#include "lib-cmab.h"
#include "lib-brres.h"
#include "lib-breff.h"
#include "lib-nintendo.h"

// Shared with lib-image-convert.c: nesting depth for the CONVERT/COPY debug
// trace below, indented two spaces per level.
extern int convert_depth;

//
///////////////////////////////////////////////////////////////////////////////
///////////////			IMG management			///////////////
///////////////////////////////////////////////////////////////////////////////

uint image_seq_num = 0;

//-----------------------------------------------------------------------------

void InitializeIMG (Image_t *img)
{
	DASSERT (img);
	noPRINT ("InitializeIMG() img=%p\n", img);

	memset (img, 0, sizeof (*img));

	img->iform = IMG_INVALID;
	img->pform = PAL_INVALID;

	img->info_fform = FF_INVALID;
	img->info_iform = IMG_INVALID;
	img->info_pform = PAL_INVALID;

	img->tform_fform0 = img->tform_fform = FF_INVALID;
	img->tform_iform0 = img->tform_iform = IMG_INVALID;
	img->tform_pform0 = img->tform_pform = PAL_INVALID;

	img->endian = &be_func;
	img->path = EmptyString;

	// img->seq_num	= ++image_seq_num;
}

///////////////////////////////////////////////////////////////////////////////

void ResetIMG (Image_t *img)
{
	DASSERT (img);
	noPRINT ("ResetIMG() img=%p, container=%p\n", img, img->container);

	FreeIMG (img, 0);
	image_seq_num--;
	InitializeIMG (img);
}

///////////////////////////////////////////////////////////////////////////////

void RemoveContainerIMG (Image_t *img)
{
	DASSERT (img);
	if (img->container)
	{
		u8 *container = img->container;
		while (img)
		{
			if (img->data && !img->data_alloced)
			{
				u8 *data = MALLOC (img->data_size);
				memcpy (data, img->data, img->data_size);
				img->data = data;
				img->data_alloced = true;
			}

			if (img->pal && !img->pal_alloced)
			{
				u8 *pal = MALLOC (img->pal_size);
				memcpy (pal, img->pal, img->pal_size);
				img->pal = pal;
				img->pal_alloced = true;
			}

			img->container = 0;
			img = img->mipmap;
		}
		FREE (container);
	}
}

///////////////////////////////////////////////////////////////////////////////

void FreeIMG (Image_t *img, // destination image, dynamic data freed
	const Image_t *src // not NULL: copy parameters, but not data
)
{
	DASSERT (img);

	//--- free data

	if (img->mipmap)
		FreeMipmapsIMG (img);

	FREE (img->container);

	if (img->data_alloced)
		FREE (img->data);

	if (img->pal_alloced)
		FREE (img->pal);

	//--- copy param & path handling

	if (!src)
	{
		if (img->path_alloced)
			FreeString (img->path);
		img->path_alloced = 0;
		img->path = EmptyString;
	}
	else if (src != img)
	{
		if (img->path_alloced)
			FreeString (img->path);

		memcpy (img, src, sizeof (*img));

		if (img->path_alloced)
			img->path = STRDUP (src->path);
	}

	//--- reset dynamic data

	img->container = 0;

	img->data = 0;
	img->data_size = 0;
	img->data_alloced = false;

	img->pal = 0;
	img->pal_size = 0;
	img->pal_alloced = false;
	img->n_pal = 0;

	img->seq_num = ++image_seq_num;
	img->conv_count++;
}

///////////////////////////////////////////////////////////////////////////////

void FreeMipmapsIMG (Image_t *img // not NULL: remove mipmaps
)
{
	if (img && img->mipmap)
	{
		ResetIMG (img->mipmap);
		FREE (img->mipmap);
		img->mipmap = 0;
	}
}

///////////////////////////////////////////////////////////////////////////////

uint CountMipmapsIMG (const Image_t *img // count mipmaps
)
{
	DASSERT (img);

	uint count = 0;
	for (;;)
	{
		img = img->mipmap;
		if (!img)
			return count;
		count++;
	}
}

///////////////////////////////////////////////////////////////////////////////

void CopyIMG (Image_t *dest, // destination image
	bool init_dest, // true: initialize 'dest' first
	const Image_t *src, // valid source image
	bool copy_mipmap // true: copy mipmaps too
)
{
	DASSERT (dest);
	noPRINT ("%*sCopyIMG() %p[%d] < %p mm=%d\n", convert_depth * 2, "", dest, init_dest, src,
		src ? CountMipmapsIMG (src) : -1);

	if (init_dest)
		InitializeIMG (dest);

	if (src != dest)
	{
		if (!init_dest)
			ResetIMG (dest);

		if (src)
		{
			DASSERT (!dest->mipmap);
			memcpy (dest, src, sizeof (*dest));

			if (copy_mipmap && dest->mipmap)
			{
				dest->mipmap = MALLOC (sizeof (*dest->mipmap));
				CopyIMG (dest->mipmap, true, src->mipmap, true);
			}
			else
				dest->mipmap = 0;

			if (dest->data_alloced || dest->container)
			{
				dest->data = MALLOC (dest->data_size);
				memcpy (dest->data, src->data, dest->data_size);
				dest->data_alloced = true;
			}

			if (dest->pal_alloced || dest->container)
			{
				dest->pal = MALLOC (dest->pal_size);
				memcpy (dest->pal, src->pal, dest->pal_size);
				dest->pal_alloced = true;
			}

			dest->container = 0;

			if (dest->path_alloced)
				dest->path = STRDUP (src->path);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

void ExtractIMG (
	// mipmaps are deleted

	Image_t *dest, // destination image
	bool init_dest, // true: initialize 'dest' first
	const Image_t *src, // valid source image

	int xbeg, // x-index of first used pixel, robust
	int xend, // x-index of first not used pixel, robust
	int ybeg, // y-index of first used pixel, robust
	int yend // y-index of first not used pixel, robust
)
{
	DASSERT (dest);
	PRINT ("ExtractIMG(,,,%d,%d,%d,%d)\n", xbeg, xend, ybeg, yend);

	if (!src)
	{
		CopyIMG (dest, init_dest, src, false);
		return;
	}

	const int width = CheckIndex2ex (src->width, &xbeg, &xend);
	const int height = CheckIndex2ex (src->height, &ybeg, &yend);
	PRINT (" => %d..%d/%u, %d..%d/%u)\n", xbeg, xend, src->width, ybeg, yend, src->height);

	if (!xbeg && xend == src->width && !ybeg && yend == src->height || width <= 0 || height <= 0)
	{
		CopyIMG (dest, init_dest, src, false);
		return;
	}

	//--- setup new image data

	Image_t temp;
	InitializeIMG (&temp);
	ConvertToRGB (&temp, src, PAL_INVALID);
	DASSERT (temp.iform == IMG_X_RGB);

	const uint bytes_per_pixel = 4;
	const uint xwidth = EXPAND8 (width);
	const uint xheight = EXPAND8 (height);
	const uint data_size = xwidth * xheight * bytes_per_pixel;
	u8 *data = MALLOC (data_size);
	memset (data, 0xff, data_size);
	PRINT (" - new image: %u*%u -> %u*%u -> %u bytes\n", width, height, xwidth, xheight, data_size);

	const uint copy_line_size = (xend - xbeg) * bytes_per_pixel;
	const uint dest_line_size = bytes_per_pixel * xwidth;
	const uint src_line_size = bytes_per_pixel * src->xwidth;
	const u8 *src1 = src->data + xbeg * bytes_per_pixel + ybeg * src_line_size;
	u8 *dest1 = data;

	for (int y = ybeg; y < yend; y++)
	{
		memcpy (dest1, src1, copy_line_size);
		dest1 += dest_line_size;
		src1 += src_line_size;
	}

	if (init_dest)
		InitializeIMG (dest);
	FreeIMG (dest, src);

	dest->data = data;
	dest->data_alloced = true;
	dest->width = width;
	dest->height = height;
	dest->xwidth = xwidth;
	dest->xheight = xheight;
	dest->data_size = data_size;
	dest->iform = IMG_X_RGB;
	NormalizeFrameIMG (dest);

	PRINT0 (" - new image: %u*%u -> %u*%u -> %u bytes\n", dest->width, dest->height, dest->xwidth,
		dest->xheight, dest->data_size);

	ResetIMG (&temp);
}

///////////////////////////////////////////////////////////////////////////////

void MoveIMG (Image_t *dest, // destination image
	bool init_dest, // true: initialize 'dest' first
	Image_t *src // NULL or source image, is initialized
)
{
	DASSERT (dest);

	if (init_dest)
		InitializeIMG (dest);

	if (src != dest)
	{
		if (!init_dest)
			ResetIMG (dest);
		if (src)
		{
			memcpy (dest, src, sizeof (*dest));
			InitializeIMG (src);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

void MoveDataIMG (Image_t *dest, // valid destination image
	Image_t *src // valid source image, is resetted
)
{
	DASSERT (dest);
	DASSERT (src);

	if (src != dest)
	{
		if (dest->container)
			FREE (dest->container);
		dest->container = src->container;
		src->container = 0;

		dest->iform = src->iform;
		if (dest->data_alloced)
			FREE (dest->data);
		dest->data = src->data;
		dest->data_size = src->data_size;
		dest->data_alloced = src->data_alloced;
		src->data_alloced = false;

		dest->pform = src->pform;
		if (dest->pal_alloced)
			FREE (dest->pal);
		dest->pal = src->pal;
		dest->pal_size = src->pal_size;
		dest->n_pal = src->n_pal;
		dest->pal_alloced = src->pal_alloced;
		src->pal_alloced = false;

		dest->mipmap = src->mipmap;
		src->mipmap = 0;

		dest->width = src->width;
		dest->height = src->height;
		dest->xwidth = src->xwidth;
		dest->xheight = src->xheight;

		ResetIMG (src);
	}
}

///////////////////////////////////////////////////////////////////////////////

void ScanDataIMG (Image_t *img, // destination image
	bool init_img, // true: initialize 'img' first
	const void *data, // data to scan
	uint data_size // size of 'data'
)
{
	DASSERT (img);

	if (init_img)
		InitializeIMG (img);
	else
		ResetIMG (img);

	if (!data || !data_size)
		return;

	// [[analyse-magic]]
	img->info_fform = GetByMagicFF (data, data_size, 0); // [[magic]]
	switch (img->info_fform)
	{
			// [[tpl-ex+]]
		case FF_CUPICON:
		case FF_TPL:
		case FF_TPLX:
		{
			const tpl_header_t *tpl;
			const tpl_pal_header_t *tp;
			const tpl_img_header_t *ti;
			if (SetupPointerTPL (data, data_size, 0, &tpl, 0, &tp, &ti, 0, 0, &be_func))
			{
				img->iform = be32 (&ti->iform);
				img->info_iform = img->iform;
				img->width = be16 (&ti->width);
				img->height = be16 (&ti->height);
				img->info_n_image = be32 (&tpl->n_image);
				// [[tpl-ex+]]
				if (img->info_fform == FF_TPLX)
				{
					tpl_header_ex_t *tplx = (tpl_header_ex_t *)tpl;
					img->width = be32 (&tplx->ex_width);
					img->height = be32 (&tplx->ex_height);
				}

				if (tp)
					img->pform = img->info_pform = be32 (&tp->pform);
			}
		}
		break;

		case FF_BTI:
		{
			const bti_header_t *bti = (bti_header_t *)data;
			img->iform = bti->iform;
			img->info_iform = img->iform;
			img->width = be16 (&bti->width);
			img->height = be16 (&bti->height);
			img->info_n_image = bti->n_image;
			img->pform = be16 (&bti->pform);
			img->info_pform = img->pform;
		}
		break;

		case FF_TEX:
		case FF_TEX_CT: // ??? [[CTCODE]] add ctcode info
		{
			const brsub_header_t *bh = (brsub_header_t *)data;
			const uint n_grp = GetSectionNumBRSUB (data, data_size, &be_func);
			const tex_info_t *ti = (tex_info_t *)(bh->grp_offset + n_grp);

			img->iform = be32 (&ti->iform);
			img->info_iform = img->iform;
			img->width = be16 (&ti->width);
			img->height = be16 (&ti->height);
			img->info_n_image = be32 (&ti->n_image);
		}
		break;

		case FF_BREFT_IMG:
		{
			const breft_image_t *bi = (breft_image_t *)data;
			img->iform = bi->iform;
			img->info_iform = img->iform;
			img->width = be16 (&bi->width);
			img->height = be16 (&bi->height);
			img->info_size = be32 (&bi->img_size);
			img->info_n_image = bi->n_mipmap + 1;
			img->n_pal = be16 (&bi->n_pal);
			if (img->n_pal && bi->pform <= PAL_RGB5A3)
				img->pform = img->info_pform = bi->pform;
		}
		break;

		case FF_CTXB:
		{
			if (data_size >= 0x18)
			{
				const u32 chunk_offset = rd_le32 ((const u8 *)data + 16);
				if (chunk_offset + 12 + 12 <= data_size
					&& !memcmp ((const u8 *)data + chunk_offset, "tex ", 4))
				{
					const u8 *tentry = (const u8 *)data + chunk_offset + 12;
					img->width = (uint)rd_le16 (tentry + 8);
					img->height = (uint)rd_le16 (tentry + 10);
					img->iform = img->info_iform = IMG_X_RGB;
					img->info_n_image = 1;
				}
			}
		}
		break;

		case FF_CMAB:
		{
			cmab_t cmab;
			cmab_entry_t entry;
			if (!ScanCMAB (&cmab, data, data_size) && cmab.texture_count
				&& !GetCMABEntry (&cmab, 0, &entry))
			{
				img->width = entry.width;
				img->height = entry.height;
				img->iform = img->info_iform = IMG_X_RGB;
				img->info_n_image = cmab.texture_count;
			}
		}
		break;

		default:
			return;
	}

	const ImageGeometry_t *geo = GetImageGeometry (img->iform);
	if (geo)
	{
		img->xwidth = ALIGN32 (img->width, geo->block_width);
		img->xheight = ALIGN32 (img->height, geo->block_height);
	}
	else
	{
		img->xwidth = EXPAND8 (img->width);
		img->xheight = EXPAND8 (img->height);
	}
}

///////////////////////////////////////////////////////////////////////////////

bool NormalizeFrameIMG (Image_t *img // pointer to valid img
)
{
	bool status = false;

	if (img->iform >= IMG_X__MIN && img->iform <= IMG_X__MAX)
	{
		const uint cell_size = img->iform == IMG_X_RGB ? 4 : 2;
		// IMG_X_GRAY & IMG_X_PAL* : size is 2

		if (img->width > 0 && img->xwidth > img->width)
		{
			const uint fill_size = (img->xwidth - img->width) * cell_size;
			u8 *row = img->data + img->width * cell_size;
			uint ih = img->height;
			while (ih-- > 0)
			{
				u8 *src = row - cell_size;
				u8 *dest = row;
				u8 *end = row + fill_size;
				while (dest < end)
					*dest++ = *src++;
				row += img->xwidth * cell_size;
			}
			status = true;
		}

		if (img->height > 0 && img->xheight > img->height)
		{
			const uint line_size = img->xwidth * cell_size;
			u8 *dest = img->data + img->height * line_size;
			u8 *end = img->data + img->xheight * line_size;
			u8 *src = dest - line_size;
			while (dest < end)
				*dest++ = *src++;
			status = true;
		}
	}

	return status;
}

///////////////////////////////////////////////////////////////////////////////

int CheckAlphaIMG (const Image_t *img, // pointer to valid img
	bool force // true: don't believe 'img->alpha_status'
)
{
	DASSERT (img);
	if (!force && img->alpha_status)
		return img->alpha_status;

	uint size = 4;
	switch (img->iform)
	{
		case IMG_X_GRAY:
			size = 2;
		case IMG_X_RGB:
		{
			const u8 *data = img->data + size - 1;
			uint ih = img->height;
			while (ih-- > 0)
			{
				uint iw = img->width;
				while (iw-- > 0)
				{
					if (*data != 0xff)
						return ((Image_t *)img)->alpha_status = 1;
					data += size;
				}
				data += (img->xwidth - img->width) * size;
			}
			return ((Image_t *)img)->alpha_status = -1;
		}

		case IMG_X_PAL4:
		case IMG_X_PAL8:
		case IMG_X_PAL14:
			if (img->n_pal)
			{
				DASSERT (img->pal);
				uint n = img->n_pal;
				const u8 *data = img->pal + 3;
				while (n-- > 0)
				{
					if (*data != 0xff)
						return ((Image_t *)img)->alpha_status = 1;
					data += 4;
				}
			}
			return ((Image_t *)img)->alpha_status = -1;

		default:
		{
			const ImageGeometry_t *geo = GetImageGeometry (img->iform);
			return ((Image_t *)img)->alpha_status = geo && !geo->has_alpha ? -1 : 0;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

bool IsGrayIMG (const Image_t *img // pointer to valid img
)
{
	DASSERT (img);
	image_format_t xform;
	NormalizeIF (&xform, img->iform, IMG_X_RGB, img->pform);
	return xform == IMG_X_GRAY;
}
