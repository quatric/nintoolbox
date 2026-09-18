
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
#include "lib-szs.h"
#include "lib-plt0.h"
#include "lib-bntx.h"
#include "lib-smdh.h"
#include "lib-nds-banner.h"
#include "lib-wii-banner.h"
#include "lib-gtx.h"
#include "lib-nitro.h"
#include "lib-nut.h"
#include "lib-retro-txtr.h"
#include "lib-ctpk.h"
#include "lib-cmab.h"
#include "lib-brres.h"
#include "lib-breff.h"
#include "lib-nintendo.h"
#include "lib-excite.h"
#include "lib-pica3ds.h"
#include "lib-gvr.h"
#include "ajpg/ajpg.h"
#include "lib-dds.h"
#include "lib-astc-file.h"

///////////////		    AssignIMG(), LoadIMG()		///////////////
///////////////////////////////////////////////////////////////////////////////

// Attaches a decoded, tightly packed width*height RGBA8 buffer to 'img'.
//
// The rest of the image pipeline (SavePNG() and friends) indexes img->data
// with the EXPAND8-rounded xwidth/xheight stride, not the raw dimensions, so
// anything whose size is not already a multiple of 8 has to be repacked into
// that stride here -- assigning xwidth=width directly trips a DASSERT in
// SavePNG(). 'rgba' must be dclib-allocated; ownership transfers to 'img'.
void AssignDecodedRGBA (Image_t *img, // pointer to valid img
	u8 *rgba, // tightly packed width*height RGBA8
	uint width, uint height,
	const endian_func_t *endian, // endianness the source format used
	ccp fname // object name, assigned
)
{
	const uint xwidth = EXPAND8 (width), xheight = EXPAND8 (height);
	u8 *data = rgba;
	if (xwidth != width || xheight != height)
	{
		data = CALLOC (1, (size_t)xwidth * xheight * 4);
		for (uint y = 0; y < height; y++)
			memcpy (data + (size_t)y * xwidth * 4, rgba + (size_t)y * width * 4, width * 4);
		FREE (rgba);
	}

	img->data = data;
	img->data_alloced = true;
	img->data_size = xwidth * xheight * 4;
	img->width = width;
	img->xwidth = xwidth;
	img->height = height;
	img->xheight = xheight;
	img->iform = img->info_iform = IMG_X_RGB;
	img->info_fform = FF_UNKNOWN;
	img->info_n_image = 1;
	img->alpha_status = 0;
	img->endian = endian;
	img->path = fname;
	img->seq_num = ++image_seq_num;
}

enumError AssignIMG (Image_t *img, // pointer to valid img
	int init_img, // <0:none, =0:reset, >0:init
	const u8 *data, // source data
	uint data_size, // size of 'data'
	uint img_index, // index of sub image, 0:main, >0:mipmaps
	bool mipmaps, // true: assign mipmaps
	const endian_func_t *endian, // endian functions to read data
	ccp fname // object name, assigned
)
{
	DASSERT (img);
	DASSERT (data);
	noPRINT ("ASSIGN-IMG: idx=%d mm=%d siz=%x %s\n", img_index, mipmaps, data_size, fname);

	if (init_img > 0)
		InitializeIMG (img);
	else if (!init_img)
		ResetIMG (img);

	if (IsDDS (data, data_size))
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeDDS_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported DDS texture: %s\n", fname);
		AssignDecodedRGBA (img, rgba, width, height, &le_func, fname);
		img->info_fform = FF_DDS;
		return PatchListIMG (img);
	}

	if (IsASTCFile (data, data_size))
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeASTCFile_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported ASTC texture: %s\n", fname);
		AssignDecodedRGBA (img, rgba, width, height, &le_func, fname);
		img->info_fform = FF_ASTC;
		return PatchListIMG (img);
	}

	if (data_size >= 4 && !memcmp (data, "TXTR", 4))
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeDSB_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported DSB texture: %s\n", fname);
		AssignDecodedRGBA (img, rgba, width, height, &le_func, fname);
		return PatchListIMG (img);
	}

	if (IsNTTF (data, data_size))
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeNTTF_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported NTTF texture: %s\n", fname);
		AssignDecodedRGBA (img, rgba, width, height, &le_func, fname);
		img->info_fform = FF_NTTF;
		return PatchListIMG (img);
	}

	// Retro Studios TXTR revisions (Metroid Prime/DKCR + Tropical Freeze +
	// Metroid Prime Remastered). None share the DSB "TXTR" magic tested
	// above, so there is no collision here; IsRetroTXTR() additionally
	// rejects both RFRM-shell magics itself. Tropical and MPR share the
	// exact same RFRM+"TXTR" shell (big-endian vs little-endian, told
	// apart by ScanMPRTXTR()'s version-pair check), so Tropical is tried
	// first and MPR only once that BE parse fails; old Retro (bare header)
	// is tried last so a truncated probe can never misroute.
	if (IsTropicalTXTR (data, data_size))
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeTropicalTXTR_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported Tropical TXTR texture: %s\n", fname);
		AssignDecodedRGBA (img, rgba, width, height, &le_func, fname);
		return PatchListIMG (img);
	}

	if (IsMPRTXTR (data, data_size))
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeMPRTXTR_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported MPR TXTR texture: %s\n", fname);
		AssignDecodedRGBA (img, rgba, width, height, &le_func, fname);
		return PatchListIMG (img);
	}

	if (IsRetroTXTR (data, data_size))
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeRetroTXTR_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported Retro TXTR texture: %s\n", fname);
		AssignDecodedRGBA (img, rgba, width, height, &be_func, fname);
		return PatchListIMG (img);
	}

	if (data_size >= 0x20 && !memcmp (data, "GCIX", 4) && !memcmp (data + 0x10, "GVRT", 4))
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeGVR_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported GVR texture: %s\n", fname);
		AssignDecodedRGBA (img, rgba, width, height, &be_func, fname);
		img->info_fform = FF_GVR;
		return PatchListIMG (img);
	}

	if (data_size >= 4 && !memcmp (data, "BNTX", 4))
	{
		// Switch texture container: decode its first texture. Multi-texture
		// containers are listed by `wszst BNTX`.
		bntx_t bntx;
		if (ScanBNTX (&bntx, data, data_size))
			return ERROR0 (ERR_INVALID_IFORM, "Invalid BNTX container: %s\n", fname);
		u8 *rgba = 0;
		uint w = 0, h = 0;
		const enumError berr = DecodeBNTX_RGBA (&rgba, &w, &h, &bntx, 0);
		ResetBNTX (&bntx);
		if (berr)
			return berr;
		AssignDecodedRGBA (img, rgba, w, h, &le_func, fname);
		img->info_fform = FF_BNTX;
		return PatchListIMG (img);
	}

	if (data_size >= SMDH_SIZE && !memcmp (data, "SMDH", 4))
	{
		// 3DS icon/title metadata: decode the large (48x48) icon, the one
		// actually shown as the application icon. The small (24x24) icon and
		// the 16 per-language titles are available via `wszst DUMP`/extract,
		// not through this single-image decode path.
		smdh_t smdh;
		if (ScanSMDH (&smdh, data, data_size))
			return ERROR0 (ERR_INVALID_IFORM, "Invalid SMDH file: %s\n", fname);
		u8 *rgba = 0;
		uint w = 0, h = 0;
		const enumError serr = DecodeSMDHIcon_RGBA (&rgba, &w, &h, &smdh, true);
		ResetSMDH (&smdh);
		if (serr)
			return serr;
		AssignDecodedRGBA (img, rgba, w, h, &le_func, fname);
		img->info_fform = FF_SMDH;
		return PatchListIMG (img);
	}

	if (IsWIBN (data, data_size))
	{
		// Wii save banner ("WIBN"): decode the 192x64 banner image. The 48x48
		// icon frames and the title/subtitle come out of `wszst XX`'s sidecar
		// (extract_wibn_metadata()), not through this single-image path.
		wibn_t wibn;
		if (ScanWIBN (&wibn, data, data_size))
			return ERROR0 (ERR_INVALID_IFORM, "Invalid WIBN save banner: %s\n", fname);
		u8 *rgba = 0;
		uint w = 0, h = 0;
		const enumError werr = DecodeWIBNImage_RGBA (&rgba, &w, &h, &wibn, WIBN_IMAGE_BANNER);
		ResetWIBN (&wibn);
		if (werr)
			return werr;
		AssignDecodedRGBA (img, rgba, w, h, &le_func, fname);
		img->info_fform = FF_WIBN;
		return PatchListIMG (img);
	}

	if (IsNDSBanner (data, data_size))
	{
		// Nintendo DS ROM banner ("banner.bin"): decode the static 32x32 icon.
		// A DSi banner's animated icon frames and the per-language titles come
		// out of `wszst XX`'s banner sidecar (extract_nds_banner_metadata()),
		// not through this single-image path.
		nds_banner_t banner;
		if (ScanNDSBanner (&banner, data, data_size))
			return ERROR0 (ERR_INVALID_IFORM, "Invalid NDS banner: %s\n", fname);
		u8 *rgba = 0;
		uint w = 0, h = 0;
		const enumError berr = DecodeNDSBannerIcon_RGBA (&rgba, &w, &h, &banner, NDS_BANNER_ICON_STATIC);
		ResetNDSBanner (&banner);
		if (berr)
			return berr;
		AssignDecodedRGBA (img, rgba, w, h, &le_func, fname);
		img->info_fform = FF_NDS_BANNER;
		return PatchListIMG (img);
	}

	if (data_size >= 4 && !memcmp (data, "Gfx2", 4))
	{
		// Wii U GX2 texture container: decode its first texture. Multi-
		// texture containers (rare for standalone .gtx; common for .gsh
		// shader files, which have none) are not separately listed yet.
		gtx_t gtx;
		if (ScanGTX (&gtx, data, data_size))
			return ERROR0 (ERR_INVALID_IFORM, "Invalid GTX container: %s\n", fname);
		u8 *rgba = 0;
		uint w = 0, h = 0;
		const enumError gerr = DecodeGTX_RGBA (&rgba, &w, &h, &gtx, 0);
		ResetGTX (&gtx);
		if (gerr)
			return gerr;
		AssignDecodedRGBA (img, rgba, w, h, &le_func, fname);
		img->info_fform = FF_GTX;
		return PatchListIMG (img);
	}

	if (data_size >= 4 && !memcmp (data, "AJPG", 4))
	{
		// ODH / "AJPG": ActImagine's baseline-JPEG-derived still image format
		// (GBA, and the Wii Message Board's photo attachments).
		u8 *rgba = 0;
		int width = 0, height = 0;
		if (!AjpgDecodeRGBA (data, data_size, &rgba, &width, &height))
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported AJPG image: %s\n", fname);
		// AjpgDecodeRGBA allocates with plain malloc(); hand the pixels to a
		// dclib-allocated buffer so the rest of the image pipeline can FREE()
		// them like any other decoded image.
		u8 *owned = MALLOC ((size_t)width * height * 4);
		memcpy (owned, rgba, (size_t)width * height * 4);
		AjpgFree (rgba);
		AssignDecodedRGBA (img, owned, width, height, &be_func, fname);
		img->info_fform = FF_AJPG;
		return PatchListIMG (img);
	}

	if (data_size >= 4 && (!memcmp (data, "BNR1", 4) || !memcmp (data, "BNR2", 4)))
	{
		u8 *rgba = 0;
		const enumError err = DecodeBNR_RGBA (&rgba, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid Wii banner: %s\n", fname);
		img->data = rgba;
		img->data_alloced = true;
		img->data_size = 96 * 32 * 4;
		img->width = img->xwidth = 96;
		img->height = img->xheight = 32;
		img->iform = img->info_iform = IMG_X_RGB;
		img->info_fform = FF_UNKNOWN;
		img->info_n_image = 1;
		img->alpha_status = 0;
		img->endian = &be_func;
		img->path = fname;
		img->seq_num = ++image_seq_num;
		return PatchListIMG (img);
	}

	if (data_size >= 4 && !memcmp (data, "RGCN", 4))
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeNCGR_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid NCGR graphics: %s\n", fname);

		// Nitro resources are normally distributed as matching foo.ncgr and
		// foo.nclr files.  Colour the sheet automatically when that companion is
		// available, but keep the indexed grayscale diagnostic view when it is not.
		const ccp dot = strrchr (fname, '.');
		if (dot && dot != fname)
		{
			const int plen = (int)(dot - fname);
			static const ccp nclr_exts[] = { ".nclr", ".NCLR", ".nclr.p", ".NCLR.P", ".nclr.P",
				".NCLR.p", ".NCLR.bin", ".nclr.bin", ".NCLR.P.bin", ".nclr.lz", ".NCLR.lz" };
			u8 *nclr_data = 0, *palette = 0;
			size_t nclr_size = 0;
			for (uint e = 0; e < sizeof (nclr_exts) / sizeof (*nclr_exts); e++)
			{
				char nclr_path[PATH_MAX];
				snprintf (nclr_path, sizeof (nclr_path), "%.*s%s", plen, fname, nclr_exts[e]);
				if (!LoadFileAlloc (nclr_path, 0, 0, &nclr_data, &nclr_size, 0, 2, 0, 0)
					&& nclr_size >= 4)
					break;
				FREE (nclr_data);
				nclr_data = 0;
				nclr_size = 0;
			}
			if (nclr_data && nclr_size >= 4)
			{
				if ((nclr_data[0] == 0x10 || nclr_data[0] == 0x11) && nclr_size >= 4)
				{
					u8 *dec = 0;
					uint dec_sz = 0;
					if (DecodeLZ10LZ11 (&dec, &dec_sz, nclr_data, (uint)nclr_size) == ERR_OK && dec)
					{
						FREE (nclr_data);
						nclr_data = dec;
						nclr_size = dec_sz;
					}
				}
				uint pal_w = 0, pal_h = 0;
				const enumError pal_err
					= DecodeNCLR_RGBA (&palette, &pal_w, &pal_h, nclr_data, (uint)nclr_size);
				if (!pal_err && palette)
				{
					const uint depth = data_size >= 0x20 ? le_func.rd32 (data + 0x10 + 0x0c) : 3;
					const bool is_4bpp = (depth == 3);
					const uint n_entries = pal_w / 8 * (pal_h / 8);
					for (uint i = 0; i < width * height; i++)
						if (rgba[4 * i + 3])
						{
							const uint index = is_4bpp ? rgba[4 * i] / 17 : rgba[4 * i];
							if (index < n_entries)
							{
								const u8 *p
									= palette + 4 * ((index / 16 * 8) * pal_w + index % 16 * 8);
								rgba[4 * i] = p[0];
								rgba[4 * i + 1] = p[1];
								rgba[4 * i + 2] = p[2];
							}
						}
				}
				FREE (palette);
				FREE (nclr_data);
			}
		}
		img->data = rgba;
		img->data_alloced = true;
		img->data_size = width * height * 4;
		img->width = img->xwidth = width;
		img->height = img->xheight = height;
		img->iform = img->info_iform = IMG_X_RGB;
		img->info_fform = FF_NCGR;
		img->info_n_image = 1;
		img->alpha_status = 0;
		img->endian = &le_func;
		img->path = fname;
		img->seq_num = ++image_seq_num;
		return PatchListIMG (img);
	}

	if (data_size >= 4 && !memcmp (data, "RLCN", 4))
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeNCLR_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid NCLR palette: %s\n", fname);
		img->data = rgba;
		img->data_alloced = true;
		img->data_size = width * height * 4;
		img->width = img->xwidth = width;
		img->height = img->xheight = height;
		img->iform = img->info_iform = IMG_X_RGB;
		img->info_fform = FF_NCLR;
		img->info_n_image = 1;
		img->alpha_status = 0;
		img->endian = &le_func;
		img->path = fname;
		img->seq_num = ++image_seq_num;
		return PatchListIMG (img);
	}

	// Nintendo 3DS image wrappers wrapping the shared PICA200 codec.  Keep them before
	// generic Nintendo detection: BTGA has no reliable four-byte magic.
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		enumError serr = ERR_NOTHING_TO_DO;
		file_format_t serr_fform = FF_UNKNOWN;
		if (data_size >= 4 && (!memcmp (data, "STEX", 4)))
		{
			serr = DecodeSTEX_RGBA (&rgba, &width, &height, data, data_size);
			serr_fform = FF_STEX;
		}
		else if (data_size >= 5 && !memcmp (data, "DMPBM", 5))
		{
			serr = DecodeDMPBM_RGBA (&rgba, &width, &height, data, data_size);
			serr_fform = FF_DMPBM;
		}
		else if (data_size >= 4 && !memcmp (data, "cmb ", 4))
		{
			serr = DecodeCMBTexture_RGBA (&rgba, &width, &height, data, data_size);
			serr_fform = FF_CMB;
		}
		else if (data_size >= 0x38 && (rd_le32 (data) == 1 || rd_le32 (data) == 0x400)
			&& (Pica3DSFilenameExt (fname, ".btga") || Pica3DSFilenameExt (fname, ".lga")))
		{
			serr = DecodeBTGA_RGBA (&rgba, &width, &height, data, data_size);
			serr_fform = FF_BTGA;
		}
		if (serr != ERR_NOTHING_TO_DO)
		{
			if (serr || !rgba)
				return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported 3DS texture: %s\n", fname);
			AssignDecodedRGBA (img, rgba, width, height, &le_func, fname);
			img->info_fform = serr_fform;
			return PatchListIMG (img);
		}
	}

	const nfmt_info_t nfmt = DetectNintendoFormat (data, data_size, fname);
	if (nfmt.type == NFMT_BFLIM || nfmt.type == NFMT_BCLIM)
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeFLIM_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported %s texture: %s\n",
				GetNintendoFormatName (nfmt.type), fname);
		AssignDecodedRGBA (img, rgba, width, height, nfmt.big_endian ? &be_func : &le_func, fname);
		img->info_fform = nfmt.type == NFMT_BFLIM ? FF_BFLIM : FF_BCLIM;
		return PatchListIMG (img);
	}

	if (nfmt.type == NFMT_TM0)
	{
		excite_tex_t tex;
		const enumError err = ScanTM0 (&tex, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported TM0 texture: %s\n", fname);
		AssignDecodedRGBA (img, tex.rgba, tex.width, tex.height, &le_func, fname);
		img->info_fform = FF_TM0;
		return PatchListIMG (img);
	}

	if (nfmt.type == NFMT_NUTEXB)
	{
		// Switch texture wrapper (Smash Ultimate etc.) -- see
		// DecodeNUTEXB_RGBA in lib-nintendo.c. Only array slice 0 / mip 0 is
		// decoded, matching the single-texture scope used above for BNTX.
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeNUTEXB_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported NUTEXB texture: %s\n", fname);
		AssignDecodedRGBA (img, rgba, width, height, &le_func, fname);
		return PatchListIMG (img);
	}

	if (nfmt.type == NFMT_CTPK)
	{
		nintendo_ctpk_t ctpk;
		enumError err = ScanCTPK (&ctpk, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported CTPK container: %s\n", fname);
		if (!ctpk.n_entries)
			return ERROR0 (ERR_INVALID_IFORM, "Empty CTPK container: %s\n", fname);
		nintendo_ctpk_entry_t entry;
		err = GetCTPKEntry (&ctpk, 0, &entry);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Failed reading CTPK texture: %s\n", fname);
		u8 *rgba = 0;
		uint width = 0, height = 0;
		err = DecodeCTPKTexture_RGBA (&rgba, &width, &height, &entry);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Failed decoding CTPK texture: %s\n", fname);
		const uint xwidth = EXPAND8 (width), xheight = EXPAND8 (height);
		u8 *padded = xwidth == width && xheight == height ? rgba : CALLOC (1, xwidth * xheight * 4);
		if (padded != rgba)
		{
			for (uint y = 0; y < height; y++)
				memcpy (padded + y * xwidth * 4, rgba + y * width * 4, width * 4);
			FREE (rgba);
		}
		img->data = padded;
		img->data_alloced = true;
		img->data_size = xwidth * xheight * 4;
		img->width = width;
		img->xwidth = xwidth;
		img->height = height;
		img->xheight = xheight;
		img->iform = img->info_iform = IMG_X_RGB;
		img->info_fform = FF_UNKNOWN;
		img->info_n_image = ctpk.n_entries;
		img->alpha_status = 0;
		img->endian = &le_func;
		img->path = fname;
		img->seq_num = ++image_seq_num;
		return PatchListIMG (img);
	}

	if (data_size >= 4 && !memcmp (data, "cmab", 4))
	{
		cmab_t cmab;
		enumError err = ScanCMAB (&cmab, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid or unsupported CMAB container: %s\n", fname);
		if (img_index >= cmab.texture_count)
			return ERROR0 (ERR_INVALID_IFORM, "CMAB texture index %u is out of range: %s\n", img_index,
				fname);
		cmab_entry_t entry;
		err = GetCMABEntry (&cmab, img_index, &entry);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid CMAB texture entry %u: %s\n", img_index, fname);
		u8 *rgba = 0;
		uint width = 0, height = 0;
		err = DecodeCMABTexture_RGBA (&rgba, &width, &height, &entry);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Failed decoding CMAB texture %u: %s\n", img_index, fname);
		AssignDecodedRGBA (img, rgba, width, height, &le_func, fname);
		img->info_fform = FF_CMAB;
		img->info_n_image = cmab.texture_count;
		return PatchListIMG (img);
	}

	if (data_size >= 0x18 && !memcmp (data, "ctxb", 4))
	{
		// Grezzo 3DS Texture Container (.ctxb)
		const u32 chunk_count = rd_le32 (data + 8);
		const u32 chunk_offset = rd_le32 (data + 16);
		const u32 tex_data_offset = rd_le32 (data + 20);

		if (chunk_offset >= data_size || tex_data_offset >= data_size)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid CTXB container: %s\n", fname);

		// Scan "tex " chunks for the first decodable texture
		uint cur_chunk_off = chunk_offset;
		bool found_tex = false;
		u8 *rgba = 0;
		uint width = 0, height = 0;

		for (uint c = 0; c < chunk_count && cur_chunk_off + 12 <= data_size; c++)
		{
			if (memcmp (data + cur_chunk_off, "tex ", 4))
				break;
			const u32 sec_size = rd_le32 (data + cur_chunk_off + 4);
			const u32 tex_count = rd_le32 (data + cur_chunk_off + 8);

			if (tex_count > 0)
			{
				// Walk every texture entry in this chunk (36 bytes each), not
				// just the first: real romfs CTXB files pack several textures
				// per "tex " chunk and the first entry is not guaranteed to
				// be a decodable format.
				for (uint t = 0; t < tex_count; t++)
				{
					if (cur_chunk_off + 12 + (uint64_t)(t + 1) * 36 > data_size)
						break;
					const u8 *tentry = data + cur_chunk_off + 12 + t * 36;
					const u32 img_size = rd_le32 (tentry);
					width = (uint)rd_le16 (tentry + 8);
					height = (uint)rd_le16 (tentry + 10);
					const u32 ctxb_fmt = rd_le32 (tentry + 12);
					const u32 data_rel_off = rd_le32 (tentry + 16);

					// Map CTXB texture format to CTR PICA format
					uint pica_fmt = 0;
					switch (ctxb_fmt)
					{
						case 0x14016756:
							pica_fmt = 8;
							break; // A8
						case 0x0000675A:
							pica_fmt = 12;
							break; // ETC1
						case 0x0000675B:
							pica_fmt = 13;
							break; // ETC1A4
						case 0x67616757:
							pica_fmt = 10;
							break; // L4
						case 0x14016757:
							pica_fmt = 7;
							break; // L8
						case 0x14016758:
							pica_fmt = 5;
							break; // LA8
						case 0x83636754:
							pica_fmt = 3;
							break; // RGB565
						case 0x80336752:
							pica_fmt = 4;
							break; // RGBA4444
						case 0x80346752:
							pica_fmt = 2;
							break; // RGBA5551
						case 0x14016752:
							pica_fmt = 0;
							break; // RGBA8
						case 0x14016754:
							pica_fmt = 1;
							break; // RGB8
						default:
							pica_fmt = 0;
							break;
					}

					const u32 tex_start = tex_data_offset + data_rel_off;
					if (tex_start < data_size)
					{
						const uint avail = data_size - tex_start;
						const uint use_size = img_size <= avail ? img_size : avail;
						enumError derr = DecodePicaTexture (&rgba, &width, &height, data + tex_start,
							width, height, pica_fmt, use_size);
						if (!derr && rgba)
						{
							found_tex = true;
							break;
						}
					}
				}
			}
			if (found_tex)
				break;
			cur_chunk_off += 12 + sec_size;
		}

		if (!found_tex || !rgba)
			return ERROR0 (ERR_INVALID_IFORM, "Failed decoding CTXB texture: %s\n", fname);

		const uint xwidth = EXPAND8 (width), xheight = EXPAND8 (height);
		u8 *padded = xwidth == width && xheight == height ? rgba : CALLOC (1, xwidth * xheight * 4);
		if (padded != rgba)
		{
			for (uint y = 0; y < height; y++)
				memcpy (padded + y * xwidth * 4, rgba + y * width * 4, width * 4);
			FREE (rgba);
		}
		img->data = padded;
		img->data_alloced = true;
		img->data_size = xwidth * xheight * 4;
		img->width = width;
		img->xwidth = xwidth;
		img->height = height;
		img->xheight = xheight;
		img->iform = img->info_iform = IMG_X_RGB;
		img->info_fform = FF_CTXB;
		img->info_n_image = 1;
		img->alpha_status = 0;
		img->endian = &le_func;
		img->path = fname;
		img->seq_num = ++image_seq_num;
		return PatchListIMG (img);
	}

	if (IsNUT (data, data_size))
	{
		nut_t nut;
		if (ScanNUT (&nut, data, data_size) == ERR_OK && nut.n_textures > 0)
		{
			uint tex_idx = img_index < nut.n_textures ? img_index : 0;
			u8 *rgba = 0;
			u32 width = 0, height = 0;
			if (DecodeNUTTextureToRGBA (&nut.textures[tex_idx], &rgba, &width, &height) && rgba
				&& width && height)
			{
				const uint xwidth = EXPAND8 (width), xheight = EXPAND8 (height);
				u8 *padded = (xwidth == width && xheight == height)
					? rgba
					: CALLOC (1, xwidth * xheight * 4);
				if (padded != rgba)
				{
					for (uint y = 0; y < height; y++)
						memcpy (padded + y * xwidth * 4, rgba + y * width * 4, width * 4);
					FREE (rgba);
				}
				img->data = padded;
				img->data_alloced = true;
				img->data_size = xwidth * xheight * 4;
				img->width = width;
				img->xwidth = xwidth;
				img->height = height;
				img->xheight = xheight;
				img->iform = img->info_iform = IMG_X_RGB;
				img->info_fform = FF_NUT;
				img->info_n_image = nut.n_textures;
				img->alpha_status = 0;
				img->endian = nut.is_big_endian ? &be_func : &le_func;
				img->path = fname;
				img->seq_num = ++image_seq_num;
				ResetNUT (&nut);
				return PatchListIMG (img);
			}
			ResetNUT (&nut);
		}
	}

	if (nfmt.type == NFMT_NSBTX || (data_size >= 4 && !memcmp (data, "BTX0", 4)))
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = DecodeNSBTX_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid NSBTX texture archive: %s\n", fname);
		AssignDecodedRGBA (img, rgba, width, height, &le_func, fname);
		img->info_fform = FF_NSBTX;
		return PatchListIMG (img);
	}

	if (nfmt.type == NFMT_NFTR || nfmt.type == NFMT_BNFR
		|| (data_size >= 4
			&& (!memcmp (data, "RTNF", 4) || !memcmp (data, "FNTR", 4)
				|| !memcmp (data, "RTFN", 4) || !memcmp (data, "NFTR", 4)
				|| !memcmp (data, "RNFB", 4) || !memcmp (data, "BNFR", 4))))
	{
		u8 *atlas = 0;
		uint width = 0, height = 0;
		char *xml = 0;
		const enumError err = DecodeNFTR_Atlas (&atlas, &width, &height, &xml, data, data_size);
		if (xml)
			FREE (xml);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid Nitro font resource: %s\n", fname);
		AssignDecodedRGBA (img, atlas, width, height, &le_func, fname);
		img->info_fform = FF_NFTR;
		return PatchListIMG (img);
	}

	if (data_size >= 4 && !memcmp (data, "5TX0", 4))
	{
		u8 *rgba = 0;
		uint width = 0, height = 0;
		const enumError err = Decode5TX_RGBA (&rgba, &width, &height, data, data_size);
		if (err)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid 5TX image: %s\n", fname);
		AssignDecodedRGBA (img, rgba, width, height, &le_func, fname);
		return PatchListIMG (img);
	}

	if (nfmt.type == NFMT_BRFNT || nfmt.type == NFMT_BRFNA)
	{
		// TGLP sheets use the normal GX texture encodings.  Their sheet pointer
		// is file-relative, so no temporary TPL container is needed here.
		uint off = 0x10;
		const uint n_sections = data_size >= 0x10 ? be16 (data + 0x0e) : 0;
		const u8 *tglp = 0;
		uint tglp_size = 0;
		for (uint i = 0; i < n_sections && off <= data_size - 8; i++)
		{
			const uint sec_size = be32 (data + off + 4);
			if (sec_size < 8 || sec_size > data_size - off)
				break;
			if (!memcmp (data + off, "TGLP", 4))
			{
				tglp = data + off;
				tglp_size = sec_size;
				break;
			}
			off += sec_size;
		}
		if (!tglp || data_size < 0x20 || tglp > data + data_size - 0x20)
			return ERROR0 (ERR_INVALID_IFORM, "No valid TGLP sheet in %s: %s\n",
				GetNintendoFormatName (nfmt.type), fname);
		const uint sheet_size = be32 (tglp + 0x0c), declared_sheets = be16 (tglp + 0x10);
		// The low byte is the real GX format id (0-14). Bit 15 of this same word
		// (checked separately, not folded into the masked format id) marks a
		// per-sheet compressed encoding used by every real archived-font (.brfna)
		// sample found -- see [[brfna-compress]] above for the codec itself.
		const uint sheet_format = be16 (tglp + 0x12);
		const bool sheet_compressed = (sheet_format & 0x8000) != 0;
		const uint iform = sheet_format & 0xFF, width = be16 (tglp + 0x18),
				   height = be16 (tglp + 0x1a);
		const uint data_off = be32 (tglp + 0x1c);
		const ImageGeometry_t *geo = GetImageGeometry (iform);
		if (!geo || !sheet_size || !declared_sheets || !width || !height || data_off > data_size)
			return ERROR0 (ERR_INVALID_IFORM, "Unsupported or invalid TGLP texture in %s\n", fname);
		const u8 *tglp_end = tglp + tglp_size;
		const u8 *img_start = data + data_off;
		uint n_sheets;
		const u8 *sheet_src
			= 0; // uncompressed: raw sheet pointer. compressed: unused (walked below).
		uint sheet_src_size = 0; // compressed: this sheet's compressed chunk size.

		if (sheet_compressed)
		{
			// Each sheet is a separate 4-byte-BE-size-prefixed compressed chunk,
			// chained back-to-back starting at data_off -- walk them to find how
			// many are really present (real files, e.g. RVL_SDK wbf1.brfna,
			// declare a sheetCount the block doesn't have room for -- see the
			// brfna_archived_font_format memory) and locate img_index's chunk.
			const u8 *p = img_start;
			uint count = 0;
			while (p + 4 <= tglp_end && p + 4 <= data + data_size && count < declared_sheets)
			{
				const uint csize = be32 (p);
				if (!csize || (u64)(p + 4 - data) + csize > (u64)(tglp_end - data))
					break;
				if (count == img_index)
				{
					sheet_src = p + 4;
					sheet_src_size = csize;
				}
				p += 4 + csize;
				count++;
			}
			n_sheets = count;
			if (!n_sheets || img_index >= n_sheets || !sheet_src)
				return ERROR0 (
					ERR_INVALID_IFORM, "Unsupported or invalid TGLP texture in %s\n", fname);
		}
		else
		{
			// Real retail multi-sheet CJK .brfna samples (RVL_SDK fonts_chn/fonts_kor
			// wbf1/wbf2 pairs) declare a sheet count that this file's own TGLP block
			// doesn't have room for -- the declared count appears to describe a
			// glyph-placement scheme shared across a family, not a promise that every
			// sheet is physically embedded here. Rather than reject the whole font,
			// clamp to however many sheets actually fit in the space this block's own
			// size (not just the whole file's remaining bytes -- CWDH/CMAP follow
			// immediately after) makes available, and decode only those.
			const uint avail_sheets
				= tglp_end > img_start ? (uint)(tglp_end - img_start) / sheet_size : 0;
			n_sheets = avail_sheets < declared_sheets ? avail_sheets : declared_sheets;
			if (!n_sheets || img_index >= n_sheets
				|| (u64)sheet_size * n_sheets > data_size - data_off)
				return ERROR0 (
					ERR_INVALID_IFORM, "Unsupported or invalid TGLP texture in %s\n", fname);
			sheet_src = img_start + sheet_size * img_index;
		}

		img->width = width;
		img->height = height;
		img->xwidth = ALIGN32 (width, geo->block_width);
		img->xheight = ALIGN32 (height, geo->block_height);
		img->alpha_status = geo->has_alpha ? 0 : -1;
		img->data_size = img->xwidth * img->xheight * geo->bits_per_pixel / 8;
		if (img->data_size > sheet_size)
			return ERROR0 (ERR_INVALID_IFORM, "Truncated TGLP texture in %s\n", fname);

		if (sheet_compressed)
		{
			u8 *decoded = MALLOC (sheet_size);
			if (!decoded || !DecompressBRFNASheet (sheet_src, sheet_src_size, decoded, sheet_size))
			{
				FREE (decoded);
				return ERROR0 (
					ERR_INVALID_IFORM, "Unsupported or invalid TGLP texture in %s\n", fname);
			}
			img->data = decoded;
			img->data_alloced = true;
		}
		else
		{
			img->data = (u8 *)sheet_src;
			img->data_alloced = false;
		}
		img->info_size = sheet_size;
		img->iform = img->info_iform = iform;
		img->info_fform = FF_UNKNOWN;
		img->info_n_image = n_sheets;
		img->pal = 0;
		img->pal_size = 0;
		img->pal_alloced = false;
		img->n_pal = 0;
		img->pform = img->info_pform = PAL_INVALID;
		img->endian = &be_func;
		img->path = fname;
		img->seq_num = ++image_seq_num;
		if (mipmaps && ++img_index < n_sheets)
		{
			img->mipmap = MALLOC (sizeof (*img->mipmap));
			if (img->mipmap)
				AssignIMG (img->mipmap, true, data, data_size, img_index, mipmaps, endian, fname);
		}
		return PatchListIMG (img);
	}

	if (nfmt.type == NFMT_BCFNT)
	{
		// BCFNT (3DS, magic "CFNT") and BFFNT (Wii U, magic "FFNT") share the same
		// FINF/TGLP/CWDH/CMAP container layout. Endianness is given by the BOM at +4
		// (FFFE = little-endian / 3DS; FEFF = big-endian / Wii U). TGLP.sheetFormat
		// (at offset 0x12 inside the TGLP section) uses the CTR/Cafe GPU format table
		// -- a different numbering from Wii GX. See PLAN.md and the long comment above
		// extract_cfnt_manifest() in wszst.c for the full story.
		//
		// Format 0 (RGBA8): stored linearly (4 bytes/pixel, no tile swizzle) by this
		// fork's own encoder. Decoded by copying into an IMG_X_RGB slab.
		// Formats 3/5/7/9/10 (RGB565/IA8/I8/IA4/I4): translated to the nearest Wii GX
		// iform and run through the standard GX tile decoder. Results are pixel-perfect
		// for files written with linear pixel data; real retail BCFNT/BFFNT sheets that
		// use CTR Morton-order or Cafe micro-tile swizzle will decode with garbled tile
		// order (the bit-depth and channel layout are still correct).
		if (data_size < 0x14)
			return ERROR0 (ERR_INVALID_IFORM, "Truncated BCFNT/BFFNT header: %s\n", fname);
		const bool bcfnt_be = data[4] == 0xFE && data[5] == 0xFF;
#define BCF16(p) (bcfnt_be ? be16 (p) : le16 (p))
#define BCF32(p) (bcfnt_be ? be32 (p) : le32 (p))
		const uint bcfnt_hdr = BCF16 (data + 6);
		if (bcfnt_hdr < 0x14 || (size_t)bcfnt_hdr + 0x14 > data_size
			|| memcmp (data + bcfnt_hdr, "FINF", 4))
			return ERROR0 (ERR_INVALID_IFORM, "No valid FINF in BCFNT/BFFNT: %s\n", fname);
		const uint finf_len = BCF32 (data + bcfnt_hdr + 4);
		if ((finf_len < 0x1C) || bcfnt_hdr + finf_len > data_size)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid FINF in BCFNT/BFFNT: %s\n", fname);
		uint ptr_glyph = BCF32 (data + bcfnt_hdr + 0x10);
		if (ptr_glyph < 8 || ptr_glyph - 8 + 0x20 > data_size
			|| memcmp (data + ptr_glyph - 8, "TGLP", 4))
		{
			ptr_glyph = BCF32 (data + bcfnt_hdr + 0x14);
			if (ptr_glyph < 8 || ptr_glyph - 8 + 0x20 > data_size
				|| memcmp (data + ptr_glyph - 8, "TGLP", 4))
				return ERR_NOTHING_TO_DO; // Outline, scalable, or glyph-only font without raster TGLP sheets
		}
		const u8 *btglp = data + (ptr_glyph - 8);
		const uint bsheet_sz = BCF32 (btglp + 0x0C);
		const uint bsheet_cnt = BCF16 (btglp + 0x10);
		const uint bctr_fmt = BCF16 (btglp + 0x12) & 0xFF;
		const uint bwidth = BCF16 (btglp + 0x18);
		const uint bheight = BCF16 (btglp + 0x1A);
		const uint bdata_off = BCF32 (btglp + 0x1C);
		// Map CTR format id → Wii GX image_format_t. -1 = no equivalent. CTR:
		// 3=RGB565 5=IA8/LA8 7=I8/L8 9=IA4/LA4 10=I4/L4; 12/13 = ETC1/ETC1A4
		// (not supported here), the rest are encodings with no GX match.
		static const int8_t ctr_to_gx[14] = {
			/* 0 RGBA8    */ IMG_RGBA32, // linear; handled below, listed for completeness
			/* 1 RGB8     */ -1,
			/* 2 RGBA5551 */ -1, // RGBA5551 ≠ GX RGB5A3 (different alpha encoding)
			/* 3 RGB565   */ IMG_RGB565,
			/* 4 RGBA4444 */ -1,
			/* 5 IA8/LA8  */ IMG_IA8,
			/* 6 HL8      */ -1,
			/* 7 I8/L8    */ IMG_I8,
			/* 8 A8       */ -1,
			/* 9 IA4/LA4  */ IMG_IA4,
			/*10 I4/L4    */ IMG_I4,
			/*11 A4       */ -1,
			/*12 ETC1     */ -1,
			/*13 ETC1A4   */ -1,
		};
		const int gx_iform = (bctr_fmt < 14) ? ctr_to_gx[bctr_fmt] : -1;
		if (!bsheet_sz || !bsheet_cnt || !bwidth || !bheight || bdata_off >= data_size)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid TGLP geometry in BCFNT/BFFNT: %s\n", fname);
		// Decide format support before the sheet-space test below: real Wii U
		// fonts store ETC1 sheets (seen: YWW MS_Gothic_16 / DynaFont_NW_Demo,
		// fmt 12) whose per-sheet size is the true compressed size but whose
		// N sheets pack tighter than sheetSz*N, so the plain "all sheets fit
		// the file" test would wrongly reject perfectly valid retail fonts as
		// corrupt geometry. Unsupported formats must report "unsupported"
		// instead. This also runs before any sheet data is dereferenced, so a
		// corrupt sheetSz cannot cause an out-of-bounds read on a format we
		// never decode.
		if (gx_iform < 0)
			return ERROR0 (ERR_INVALID_IFORM, "BCFNT/BFFNT: unsupported sheet format %u: %s\n",
				bctr_fmt, fname);
		if ((uint64_t)bsheet_sz * bsheet_cnt > data_size - bdata_off)
			return ERROR0 (ERR_INVALID_IFORM, "Invalid TGLP geometry in BCFNT/BFFNT: %s\n", fname);
		if (img_index >= bsheet_cnt)
			return ERROR0 (ERR_INVALID_IFORM, "Sheet index %u >= sheet count %u in %s\n", img_index,
				bsheet_cnt, fname);
		const u8 *bsheet = data + bdata_off + (size_t)bsheet_sz * img_index;

		if (bctr_fmt == 0)
		{
			// RGBA8 linear: the encoder stores 4 bytes/pixel row-major without any
			// hardware tile swizzle, so we copy directly into an xwidth-strided slab.
			if (bsheet_sz < bwidth * bheight * 4u)
				return ERROR0 (
					ERR_INVALID_IFORM, "Truncated RGBA8 sheet in BCFNT/BFFNT: %s\n", fname);
			const uint bxw = EXPAND8 (bwidth), bxh = EXPAND8 (bheight);
			u8 *padded = CALLOC (1, bxw * bxh * 4);
			if (!padded)
				return ERROR0 (ERR_OUT_OF_MEMORY, "Out of memory: BCFNT/BFFNT RGBA8: %s\n", fname);
			for (uint y = 0; y < bheight; y++)
				memcpy (padded + y * bxw * 4, bsheet + y * bwidth * 4, bwidth * 4);
			img->data = padded;
			img->data_alloced = true;
			img->data_size = bxw * bxh * 4;
			img->width = bwidth;
			img->xwidth = bxw;
			img->height = bheight;
			img->xheight = bxh;
			img->iform = img->info_iform = IMG_X_RGB;
			img->alpha_status = 0;
		}
		else
		{
			// (gx_iform decided above, before the geometry sanity checks.)
			const ImageGeometry_t *geo = GetImageGeometry ((image_format_t)gx_iform);
			if (!geo)
				return ERROR0 (ERR_INVALID_IFORM,
					"BCFNT/BFFNT: no geometry for GX iform %d (CTR %u): %s\n", gx_iform, bctr_fmt,
					fname);
			img->data = (u8 *)bsheet;
			img->data_alloced = false;
			img->data_size = bsheet_sz;
			img->width = bwidth;
			img->xwidth = ALIGN32 (bwidth, geo->block_width);
			img->height = bheight;
			img->xheight = ALIGN32 (bheight, geo->block_height);
			img->iform = img->info_iform = (image_format_t)gx_iform;
			img->alpha_status = geo->has_alpha ? 0 : -1;
		}
#undef BCF16
#undef BCF32
		img->pal = 0;
		img->pal_size = 0;
		img->pal_alloced = false;
		img->n_pal = 0;
		img->pform = img->info_pform = PAL_INVALID;
		img->info_fform = FF_UNKNOWN;
		img->info_n_image = bsheet_cnt;
		img->endian = bcfnt_be ? &be_func : &le_func;
		img->path = fname;
		img->seq_num = ++image_seq_num;
		if (mipmaps && ++img_index < bsheet_cnt)
		{
			img->mipmap = MALLOC (sizeof (*img->mipmap));
			if (img->mipmap)
				AssignIMG (img->mipmap, true, data, data_size, img_index, mipmaps, endian, fname);
		}
		return PatchListIMG (img);
	}

	// [[analyse-magic]]
	const file_format_t fform = GetByMagicFF (data, data_size, data_size);

	image_format_t iform = IMG_INVALID;
	palette_format_t pform = PAL_INVALID;
	uint width = 0, height = 0, n_pal = 0, psize = 0, n_img = 0;
	const u8 *idata = 0, *pdata = 0;
	bool calc_geo = false; // true: calculate geometry for mipmaps

	switch (fform)
	{
			// [[tpl-ex+]]
			// case FF_CUPICON: never defined by magic
		case FF_TPL:
		case FF_TPLX:
		{
			const tpl_header_t *tpl;
			const tpl_pal_header_t *tp;
			const tpl_img_header_t *ti;

			if (SetupPointerTPL (
					data, data_size, img_index, &tpl, 0, &tp, &ti, &pdata, &idata, &be_func))
			{
				iform = be32 (&ti->iform);
				width = be16 (&ti->width);
				height = be16 (&ti->height);
				n_img = be32 (&tpl->n_image);

				// [[tpl-ex+]]
				if (fform == FF_TPLX)
				{
					tpl_header_ex_t *tplx = (tpl_header_ex_t *)tpl;
					width = be32 (&tplx->ex_width);
					height = be32 (&tplx->ex_height);
				}

				if (tp)
				{
					pform = be32 (&tp->pform);
					n_pal = be16 (&tp->n_entry);
					psize = pdata < idata ? idata - pdata : data + data_size - pdata;
					noPRINT ("PALETTE: %u*%02x[%s], off = 0x%zx, size = 0x%x, n= %u\n", n_pal,
						pform, GetImageFormatName (pform, "?"), pdata - data, psize, n_pal);
				}
			}
		}
		break;

		case FF_BTI:
		{
			const bti_header_t *bti = (bti_header_t *)data;
			iform = bti->iform;
			idata = data + be32 (&bti->data_off);
			width = be16 (&bti->width);
			height = be16 (&bti->height);
			n_img = bti->n_image;
			calc_geo = true;

			const u32 pal_off = be32 (&bti->pal_off);
			if (pal_off && pal_off < data_size)
			{
				pform = be16 (&bti->pform);
				pdata = data + pal_off;
				psize = pdata < idata ? idata - pdata : data_size - pal_off;
			}
		}
		break;

		case FF_TEX:
		case FF_TEX_CT: // ??? [[CTCODE]] add ctcode info
		{
			const brsub_header_t *bh = (brsub_header_t *)data;
			const uint n_grp = GetSectionNumBRSUB (data, data_size, endian);
			const tex_info_t *ti = (tex_info_t *)(bh->grp_offset + n_grp);
			uint grp_off = endian->rd32 (&bh->grp_offset);

			if (grp_off < data_size)
			{
				iform = endian->rd32 (&ti->iform);
				width = endian->rd16 (&ti->width);
				height = endian->rd16 (&ti->height);
				n_img = endian->rd32 (&ti->n_image);
				calc_geo = true;
				idata = (u8 *)data + grp_off;
			}
		}
		break;

		case FF_BREFT_IMG:
			if (data_size > sizeof (breft_image_t))
			{
				const breft_image_t *bi = (breft_image_t *)data;
				iform = bi->iform;
				width = be16 (&bi->width);
				height = be16 (&bi->height);
				idata = (u8 *)data + sizeof (*bi);
				n_img = bi->n_mipmap + 1;
				calc_geo = true;

				// REFT keeps an indexed image's palette inline, immediately after
				// the complete image+mipmap payload. BrawlCrate's REFTImageHeader is
				// the authoritative 0x20-byte layout; the old placeholder fields hid
				// pform/colorCount/paletteSize and made every CI4/CI8 effect fail.
				const uint image_size = be32 (&bi->img_size);
				const uint palette_size = be32 (&bi->pal_size);
				const uint palette_count = be16 (&bi->n_pal);
				const size_t palette_off = sizeof (*bi) + (size_t)image_size;
				if (palette_count && bi->pform <= PAL_RGB5A3 && palette_size >= palette_count * 2
					&& palette_off <= data_size && palette_size <= data_size - palette_off)
				{
					pform = bi->pform;
					n_pal = palette_count;
					psize = palette_size;
					pdata = data + palette_off;
				}
			}
			break;

		case FF_PLT0:
			// PLT0 is palette-only – call dedicated loader and return directly.
			{
				enumError perr = LoadPLT0 (img, data, data_size);
				if (perr)
					return perr;
				img->info_fform = FF_PLT0;
				img->path = fname;
				img->seq_num = ++image_seq_num;
				return PatchListIMG (img);
			}

		default:
			return opt_ignore || fform == FF_UNKNOWN
				? ERR_WARNING
				: ERROR0 (ERR_INVALID_IFORM, "No (supported) image file [file type=%s]: %s\n",
					  GetNameFF (0, fform), fname);
	}

	const ImageGeometry_t *geo = GetImageGeometry (iform);
	if (geo && calc_geo)
	{
		noPRINT_IF (
			img_index, "BASE IMAGE: %3u*%-3u %6zu\n", width, height, data + data_size - idata);
		uint n;
		for (n = img_index; n > 0; n--)
		{
			uint img_size;
			CalcImageGeometry (iform, width, height, 0, 0, 0, 0, &img_size);
			idata += img_size;
			width /= 2;
			height /= 2;
			noPRINT ("NEXT IMAGE: %3u*%-3u %6zu %6u\n", width, height, data + data_size - idata,
				img_size);
		}
	}

	if (!idata && data_size < 0x40)
	{
		// small => only a fragment (e.g. header) => be silent
		return ERR_INVALID_IFORM;
	}

	uint delta = idata - data;
	if (!idata || delta >= data_size)
		return ERROR0 (ERR_INVALID_IFORM, "Invalid image format [file type=%s]: %s\n",
			GetNameFF (0, fform), fname);

	img->width = width;
	img->height = height;
	img->xwidth = geo ? ALIGN32 (width, geo->block_width) : EXPAND8 (width);
	img->xheight = geo ? ALIGN32 (height, geo->block_height) : EXPAND8 (height);
	img->alpha_status = geo && !geo->has_alpha ? -1 : 0;

	// img->container	= set by caller if needed/wanted!
	img->data = (u8 *)idata;
	img->info_size = data_size - delta;
	img->data_size = geo ? img->xwidth * img->xheight * geo->bits_per_pixel / 8 : img->info_size;
	img->data_alloced = false;
	img->iform = iform;
	img->info_iform = iform;
	img->info_fform = fform;
	img->info_n_image = n_img;

	img->pal = (u8 *)pdata;
	img->pal_size = psize;
	img->pal_alloced = false;
	img->n_pal = n_pal;
	img->pform = pform;
	img->info_pform = pform;

	img->endian = endian;
	img->path = fname;
	img->seq_num = ++image_seq_num;

	noPRINT ("-> %u*%u->%u*%u [%u=0x%x]\n", img->width, img->height, img->xwidth, img->xheight,
		img->data_size, img->data_size);

	if (mipmaps && ++img_index < n_img)
	{
		DASSERT (!img->mipmap);
		img->mipmap = MALLOC (sizeof (*img->mipmap));
		AssignIMG (img->mipmap, true, data, data_size, img_index, mipmaps, endian, fname);
	}

	return PatchListIMG (img);
}

///////////////////////////////////////////////////////////////////////////////

enumError LoadIMG (Image_t *img, // pointer to valid img
	bool init_img, // true: initialize 'img'
	ccp fname, // filename of source
	uint img_index, // index of sub image, 0:main, >0:mipmaps
	bool mipmaps, // true: load and assign mipmaps
	bool allow_subfile, // allow to extract szs sub files
	bool ignore_no_file // ignore if file does not exists
						// and return warning ERR_NOT_EXISTS
)
{
	DASSERT (img);
	DASSERT (fname);
	TRACE ("LoadIMG(%p,%d,%d) fname=%s\n", img, init_img, ignore_no_file, fname);

	GenericImgParam_t genpar;
	if (CheckGenericIMG (&genpar, fname, ignore_no_file))
		return CreateGenericIMG (&genpar, img, init_img);

	if (init_img)
		InitializeIMG (img);
	else
		ResetIMG (img);

	szs_extract_t eszs;
	if (allow_subfile)
	{
		enumError err = ExtractSZS (&eszs, true, fname, 0, ignore_no_file);
		if (err)
			return err;
	}
	else
		InitializeExtractSZS (&eszs);

	if (!eszs.data)
	{
		File_t F;
		enumError err = OpenFILE (&F, true, fname, ignore_no_file, false);
		if (err || !F.f)
			return err;

		if (ignore_no_file && !S_ISREG (F.st.st_mode))
		{
			ResetFile (&F, 0);
			return ERR_WARNING;
		}

		u8 buf[0x200];
		size_t read_stat = fread (buf, 1, sizeof (buf), F.f);
		// [[analyse-magic]]
		const file_format_t fform = GetByMagicFF (buf, read_stat, 0);
		if (fform == FF_PNG)
		{
			err = ReadPNG (img, mipmaps, &F, buf, read_stat);
			img->path = F.fname;
			F.fname = 0;
			ResetFile (&F, 0);
			return err;
		}

		// use 'eszs' data structure to hold dynamic data
		eszs.data_size = F.st.st_size;
		eszs.data = MALLOC (eszs.data_size);
		eszs.data_alloced = true;

		memcpy (eszs.data, buf, read_stat);
		if (read_stat < eszs.data_size)
		{
			const uint read_len = eszs.data_size - read_stat;
			read_stat = fread (eszs.data + read_stat, 1, read_len, F.f);
			if (read_stat != read_len)
			{
				ERROR1 (ERR_READ_FAILED, "Can't read file: %s\n", fname);
				ResetFile (&F, 0);
				return ERR_READ_FAILED;
			}
		}
		ResetFile (&F, 0);
		FreeString (eszs.fname);
		eszs.fname = STRDUP (fname);
	}

	const nfmt_info_t nfmt = DetectNintendoFormat (eszs.data, eszs.data_size, fname);
	if (nfmt.type == NFMT_FZIP)
	{
		u8 *decoded = 0;
		uint decoded_size = 0;
		enumError derr = DecodeFZIP (&decoded, &decoded_size, eszs.data, eszs.data_size);
		if (derr)
		{
			ResetExtractSZS (&eszs);
			return ERROR0 (ERR_INVALID_DATA, "Invalid FZIP stream: %s\n", fname);
		}
		if (eszs.data_alloced)
			FREE (eszs.data);
		eszs.data = decoded;
		eszs.data_size = decoded_size;
		eszs.data_alloced = true;
	}
	else if (nfmt.type == NFMT_STPL)
	{
		u8 *decoded = 0;
		uint decoded_size = 0;
		enumError derr = DecodeCamelot (&decoded, &decoded_size, eszs.data, eszs.data_size);
		if (derr)
		{
			ResetExtractSZS (&eszs);
			return ERROR0 (ERR_INVALID_DATA, "Invalid Camelot STPL stream: %s\n", fname);
		}
		if (eszs.data_alloced)
			FREE (eszs.data);
		eszs.data = decoded;
		eszs.data_size = decoded_size;
		eszs.data_alloced = true;
		eszs.endian = &be_func;
	}
	else if (nfmt.type == NFMT_LZ10 || nfmt.type == NFMT_LZ11)
	{
		// WarioWare: D.I.Y. Showcase / "WarioWare Snapped!" (DSiWare) stores
		// its Nitro graphics (NCGR/NCLR/NCER/NANR) LZ11-compressed, and every
		// decompressed resource is itself wrapped in a 4-byte size-prefix
		// record before the real RGCN/RLCN/RECN/RNAN magic -- see the
		// NITRO_SIZE_PREFIX comment in DetectNintendoFormat() (lib-nintendo.c)
		// for the verified byte layout. Decompress here, then peel off that
		// wrapper if present, so AssignIMG()'s magic checks (which look for
		// RGCN/RLCN literally at offset 0) see the real resource.
		u8 *decoded = 0;
		uint decoded_size = 0;
		enumError derr = DecodeLZ10LZ11 (&decoded, &decoded_size, eszs.data, eszs.data_size);
		if (derr)
		{
			ResetExtractSZS (&eszs);
			return ERROR0 (ERR_INVALID_DATA, "Invalid LZ%s stream: %s\n",
				nfmt.type == NFMT_LZ10 ? "10" : "11", fname);
		}
		if (eszs.data_alloced)
			FREE (eszs.data);
		eszs.data = decoded;
		eszs.data_size = decoded_size;
		eszs.data_alloced = true;

		const nfmt_info_t inner = DetectNintendoFormat (eszs.data, eszs.data_size, fname);
		if (inner.payload_offset && inner.payload_offset < eszs.data_size)
		{
			u8 *stripped = MALLOC (eszs.data_size - inner.payload_offset);
			memcpy (
				stripped, eszs.data + inner.payload_offset, eszs.data_size - inner.payload_offset);
			FREE (eszs.data);
			eszs.data = stripped;
			eszs.data_size -= inner.payload_offset;
		}
	}

	enumError err = AssignIMG (
		img, -1, eszs.data, eszs.data_size, img_index, mipmaps, eszs.endian, eszs.fname);
	if (!err)
	{
		if (eszs.data_alloced)
		{
			eszs.data_alloced = false;
			img->container = eszs.data;
		}
		img->path_alloced = true;
		eszs.fname = 0;
	}

	ResetExtractSZS (&eszs);
	return err;
}

//
