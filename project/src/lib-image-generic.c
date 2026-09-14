
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
#include "lib-bzip2.h"
#include "dclib-utf8.h"

#include "red-36.inc"
#include "blue-40.inc"
#include "cup-images.inc"

///////////////////////////////////////////////////////////////////////////////
///////////////			create generic images		///////////////
///////////////////////////////////////////////////////////////////////////////

enum generic_img_cmd_t
{
	VICMD_BLANK,
	VICMD_TEXT,
	VICMD_CUP_IMAGES,
	VICMD_CUP_ICON,
	VICMD_CUP_FILE,
};

//-----------------------------------------------------------------------------

enum generic_img_options_t
{
	VIOPT_FONT = 0x0001,
	VIOPT_SIZE = 0x0002,
	VIOPT_COLOR = 0x0004,
};

//-----------------------------------------------------------------------------

static const KeywordTab_t generic_img_key[] = {
	{ VICMD_BLANK, "BLANK", 0, VIOPT_SIZE | VIOPT_COLOR },
	{ VICMD_TEXT, "TEXT", 0, VIOPT_FONT },
	{ VICMD_CUP_IMAGES, "CUP-IMAGE", "CUPIMAGE", 0 },
	{ VICMD_CUP_ICON, "CUP-ICON", "CUPICON", 0 },
	{ VICMD_CUP_FILE, "CUP-FILE", "CUPFILE", 0 },
	{ 0, 0, 0, 0 },
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

bool CheckGenericIMG // returns TRUE if command syntax is ok
	(GenericImgParam_t *par, // parameter to setup
		ccp fname, // filename to check
		bool ignore_unknown // true: ignore unknown (sub-)commands
	)
{
	DASSERT (par);
	memset (par, 0, sizeof (*par));
	par->ignore_unknown = ignore_unknown;

	if (fname && *fname == ':')
	{
		ccp eq = strchr (fname, '=');
		if (eq)
		{
			par->cmd_name.ptr = fname + 1;
			par->cmd_name.len = eq - par->cmd_name.ptr;
			par->param = MemByString0 (eq + 1);
		}
		else
		{
			par->cmd_name = MemByString0 (fname + 1);
			par->param = NullMem;
		}

		if (par->cmd_name.len && !memchr (par->cmd_name.ptr, '/', par->cmd_name.len)
			&& !memchr (par->cmd_name.ptr, '.', par->cmd_name.len))
		{
			return true;
		}
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////////

static enumError CreateGenericTextIMG (GenericImgParam_t *par, // valid parameters
	Image_t *img // pointer to valid img
)
{
	DASSERT (par);
	DASSERT (img);

	static char search[] = "ÄÖÜàáâäèéíñóôúüßτ "; // NBSP (0xa0) at end of string
	static char replace[] = "AOUaaaaeeinoouusT ";
	static u32 codelist[sizeof (replace)] = { 0 };

	if (!codelist[0])
	{
		ccp src = search;
		u32 *dest = codelist;
		for (;;)
		{
			const u32 code = ScanUTF8AnsiChar (&src);
			if (!code)
				break;
			*dest++ = code;
		}
		// HexDump16(stdout,0,0,codelist,sizeof(codelist));
	}

	BZ2Manager_t *font = par->font ? par->font : &blue_40_bin_mgr;
	DecodeBZIP2Manager (font);

	u32 *offset_list = (u32 *)(font->data + be32 (font->data));
	ccp char_list = (ccp)font->data + 4;

	ccp src = par->param.ptr;
	ccp end = src + par->param.len;
	if (logging >= 2)
		fprintf (stdlog, "# GenericText: |%.*s|\n", par->param.len, par->param.ptr);

	while (src < end)
	{
		u32 code = ScanUTF8AnsiChar (&src);
		if (code <= ' ' || code == '_')
			code = ' ';
		else if (code >= 0x80)
		{
			for (u32 *p = codelist; *p; p++)
				if (*p == code)
				{
					PRINT0 (" REPLACE |#%u| -> %zu |%c|\n", code, p - codelist,
						(uchar)replace[p - codelist]);
					code = (uchar)replace[p - codelist];
					break;
				}
			if (code >= 0x100)
				continue;
		}

		ccp found = strchr (char_list, code);
		PRINT0 (" |%c| : %zd\n", code, found ? found - char_list : -1);
		if (!found)
			continue;

		const int chidx = found - char_list;
		const u32 off = ntohl (offset_list[chidx]);
		const u8 *data = font->data + off;
		const int size = ntohl (offset_list[chidx + 1]) - off;

		if (img->width)
		{
			Image_t img2;
			AssignIMG (&img2, true, data, size, 0, false, &be_func, ":TEXT");
			PatchIMG (img, img, &img2, PIM_INS_RIGHT);
			ResetIMG (&img2);
		}
		else
			AssignIMG (img, false, data, size, 0, false, &be_func, ":TEXT");
	}

	return ERR_OK;
};

///////////////////////////////////////////////////////////////////////////////

static enumError CreateGenericCupImagesIMG (GenericImgParam_t *par, // valid parameters
	Image_t *img // pointer to valid img
)
{
	DASSERT (par);
	DASSERT (img);

	enum
	{
		I_ARROWS,
		I_ORIG,
		I_SWAPPED,
		I_WIIMM,

		I_PIXEL = 0x10, // N(pixel) = ( ( VAL & I_M_PIXEL ) >> I_S_PIXEL ) * 8 + 8
		I_M_PIXEL = 0xf0,
		I_S_PIXEL = 4,
	};

	static const KeywordTab_t keytab[] = {
		{ I_ARROWS, "ARROWS", 0, 0 },
		{ I_ORIG, "ORIGINAL", 0, 0 },
		{ I_SWAPPED, "SWAPPED", 0, 0 },
		{ I_WIIMM, "WIIMM", 0, 0 },

		{ 0 * I_PIXEL, "8PIXELS", 0, I_M_PIXEL },
		{ 1 * I_PIXEL, "16PIXELS", 0, I_M_PIXEL },
		{ 2 * I_PIXEL, "24PIXELS", 0, I_M_PIXEL },
		{ 3 * I_PIXEL, "32PIXELS", 0, I_M_PIXEL },
		{ 4 * I_PIXEL, "40PIXELS", 0, I_M_PIXEL },
		{ 5 * I_PIXEL, "48PIXELS", 0, I_M_PIXEL },
		{ 6 * I_PIXEL, "56PIXELS", 0, I_M_PIXEL },
		{ 7 * I_PIXEL, "64PIXELS", 0, I_M_PIXEL },
		{ 8 * I_PIXEL, "72PIXELS", 0, I_M_PIXEL },
		{ 9 * I_PIXEL, "80PIXELS", 0, I_M_PIXEL },
		{ 10 * I_PIXEL, "88PIXELS", 0, I_M_PIXEL },
		{ 11 * I_PIXEL, "96PIXELS", 0, I_M_PIXEL },
		{ 12 * I_PIXEL, "104PIXELS", 0, I_M_PIXEL },
		{ 13 * I_PIXEL, "112PIXELS", 0, I_M_PIXEL },
		{ 14 * I_PIXEL, "120PIXELS", 0, I_M_PIXEL },
		{ 15 * I_PIXEL, "128PIXELS", 0, I_M_PIXEL },

		{ 0, 0, 0, 0 },
	};

	mem_t parlist = par->param;
	while (parlist.len)
	{
		ccp comma = memchr (parlist.ptr, ',', parlist.len);
		ccp plus = memchr (parlist.ptr, '+', parlist.len);
		if (!comma || plus && plus < comma)
			comma = plus;
		mem_t text = BeforeMem (parlist, comma);
		parlist = BehindMem (parlist, comma ? comma + 1 : 0);

		const KeywordTab_t *cmd = ScanKeywordEx (0, text.ptr, text.len, LOUP_UPPER, keytab);
		if (!cmd)
		{
			if (!par->ignore_unknown)
				ERROR0 (ERR_NOT_EXISTS, "Unknown image name: %.*s\n", text.len, text.ptr);
			return ERR_NOT_EXISTS;
		}

		if (logging >= 1)
			fprintf (stdlog, "# GenericCupImage: %s\n", cmd->name1);

		BZ2Manager_t *bz2 = 0;
		switch (cmd->id)
		{
			case I_ARROWS:
				bz2 = &cup_arrows_tpl_mgr;
				break;
			case I_ORIG:
				bz2 = &cup_orig_tpl_mgr;
				break;
			case I_SWAPPED:
				bz2 = &cup_swapped_tpl_mgr;
				break;
			case I_WIIMM:
				bz2 = &cup_wiimm_tpl_mgr;
				break;
		}

		if (bz2)
		{
			DecodeBZIP2Manager (bz2);
			if (img->width)
			{
				Image_t img2;
				AssignIMG (&img2, true, bz2->data, bz2->size, 0, false, &be_func, ":IMAGE");
				PatchIMG (img, img, &img2, PIM_INS_BOTTOM);
				ResetIMG (&img2);
			}
			else
				AssignIMG (img, false, bz2->data, bz2->size, 0, false, &be_func, ":BOTTOM");
		}
	}
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError CreateGenericCupIconIMG (GenericImgParam_t *par, // valid parameters
	Image_t *img // pointer to valid img
)
{
	DASSERT (par);
	DASSERT (img);

	Color_t col = { .val = 0 };
	GenericImgParam_t mypar = *par;
	mem_t parlist = mypar.param;
	enumError err = ERR_OK;

	while (parlist.len)
	{
		ccp pipe = memchr (parlist.ptr, '|', parlist.len);
		ccp nl = memchr (parlist.ptr, '\n', parlist.len);
		if (!pipe || nl && nl < pipe)
			pipe = nl;
		mem_t text = BeforeMem (parlist, pipe);
		parlist = BehindMem (parlist, pipe ? pipe + 1 : 0);
		PRINT0 ("%d+%d : %.*s\n", text.len, parlist.len, text.len, text.ptr);

		if (text.len > 2 && *text.ptr == ':' && !memcmp (text.ptr + text.len - 2, "px", 2))
		{
			par->force_width = str2ul (text.ptr + 1, 0, 10);
			PRINT0 (">>>>> width %u\n", par->force_width);
			continue;
		}

		if (!memcmp (text.ptr, ":test", 5))
		{
			par->test_mode = true;
			continue;
		}

		Image_t cupicon;
		if (text.len && *text.ptr == ':')
		{
			text = MidMem (text, 1, text.len);
			if (!text.len || *text.ptr != ':')
			{
				mypar.param = text;
				InitializeIMG (&cupicon);
				CreateGenericCupImagesIMG (&mypar, &cupicon);
				goto append;
			}
		}

		if (logging >= 1)
			fprintf (stdlog, "# GenericCupIcon: |%.*s|\n", text.len, text.ptr);

		err = CreateIMG (&cupicon, true, 128, 128, col);
		if (err)
			break;

		ccp minus = memchr (text.ptr, '-', text.len);
		if (minus)
		{
			mypar.font = &red_36_bin_mgr;
			mypar.param = BeforeMem (text, minus);
			Image_t num;
			InitializeIMG (&num);
			err = CreateGenericTextIMG (&mypar, &num);
			if (err)
				break;
			if (num.width > 128)
				ResizeIMG (&num, false, 0, 128, num.height);
			PatchIMG (&cupicon, &cupicon, &num, PIM_RIGHT | PIM_TOP);
			ResetIMG (&num);

			text = BehindMem (text, minus + 1);
		}

		if (text.len)
		{
			mypar.font = &blue_40_bin_mgr;
			mypar.param = text;
			Image_t name;
			InitializeIMG (&name);
			err = CreateGenericTextIMG (&mypar, &name);
			if (err)
				break;
			if (name.width > 128)
				ResizeIMG (&name, false, 0, 128, name.height);
			PatchIMG (&cupicon, &cupicon, &name, PIM_BOTTOM);
			ResetIMG (&name);
		}

	append:;
		if (img->height)
			PatchIMG (img, img, &cupicon, PIM_INS_BOTTOM);
		else
			CopyIMG (img, false, &cupicon, false);
		ResetIMG (&cupicon);
	}

	img->is_cup_icon = true;
	img->test_mode = par->test_mode;
	return err;
}

///////////////////////////////////////////////////////////////////////////////

static enumError CreateGenericCupFileIMG (GenericImgParam_t *par, // valid parameters
	Image_t *img // pointer to valid img
)
{
	DASSERT (par);
	DASSERT (img);

	u8 *data = 0;
	size_t size;
	enumError err = LoadFileAlloc (par->param.ptr, 0, 0, &data, &size, 1000000, 0, 0, false);
	if (!err)
	{
		GenericImgParam_t mypar = *par;
		mypar.param.ptr = (ccp)data;
		mypar.param.len = size;
		err = CreateGenericCupIconIMG (&mypar, img);
	}
	if (data)
		FREE (data);
	return err;
}

///////////////////////////////////////////////////////////////////////////////

enumError CreateGenericIMG (GenericImgParam_t *par, // valid parameters
	Image_t *img, // pointer to valid img
	bool init_img // true: initialize 'img'
)
{
	DASSERT (par);
	DASSERT (img);
	if (init_img)
		InitializeIMG (img);
	else
		ResetIMG (img);

	par->cmd = ScanKeywordEx (0, par->cmd_name.ptr, par->cmd_name.len, LOUP_UPPER, generic_img_key);
	if (!par->cmd)
	{
		if (!par->ignore_unknown)
			ERROR0 (ERR_NOT_EXISTS, "Invalid keyword for virtual image: %.*s\n", par->cmd_name.len,
				par->cmd_name.ptr);
		return ERR_NOT_EXISTS;
	}

	mem_t param = par->param;
	if (par->cmd->opt & VIOPT_FONT)
	{
		ccp comma = memchr (param.ptr, ',', param.len);
		mem_t scan = BeforeMem (param, comma);
		param = BehindMem (param, comma ? comma + 1 : 0);

		if (scan.len)
			par->font = tolower (*scan.ptr) == 'r' ? &red_36_bin_mgr : &blue_40_bin_mgr;
	}

	if (par->cmd->opt & VIOPT_SIZE)
	{
		ccp comma = memchr (param.ptr, ',', param.len);
		mem_t scan = BeforeMem (param, comma);
		param = BehindMem (param, comma ? comma + 1 : 0);

		PRINT0 (" > scan |%.*s|%.*s|\n", scan.len, scan.ptr, param.len, param.ptr);
		if (scan.len)
		{
			char *next;
			par->width = str2ul (scan.ptr, &next, 10);
			par->height = *next == 'x' ? str2ul (next + 1, 0, 10) : par->width;
		}
	}

	if (par->cmd->opt & VIOPT_COLOR)
	{
		ccp comma = memchr (param.ptr, ',', param.len);
		mem_t scan = BeforeMem (param, comma);
		param = BehindMem (param, comma ? comma + 1 : 0);

		PRINT0 (" > scan |%.*s|%.*s|\n", scan.len, scan.ptr, param.len, param.ptr);
		if (scan.len)
		{
			u32 col = str2ul (scan.ptr, 0, 16);
			if (scan.len <= 6)
				col = col << 8 | 0xff;
			par->color.val = htonl (col);
		}
	}

	PRINT0 ("Virtual image found: |%.*s| = |%.*s| [%dx%d,%08x]\n", par->cmd_name.len,
		par->cmd_name.ptr, param.len, param.ptr, par->width, par->height, par->color);

	par->param = param;
	enumError err;
	switch (par->cmd->id)
	{
		case VICMD_BLANK:
			if (!par->width)
				par->width = 1;
			if (!par->height)
				par->height = 1;
			err = CreateIMG (img, false, par->width, par->height, par->color);
			break;

		case VICMD_TEXT:
			err = CreateGenericTextIMG (par, img);
			break;

		case VICMD_CUP_IMAGES:
			err = CreateGenericCupImagesIMG (par, img);
			break;

		case VICMD_CUP_ICON:
			err = CreateGenericCupIconIMG (par, img);
			break;

		case VICMD_CUP_FILE:
			err = CreateGenericCupFileIMG (par, img);
			break;

		default:
			return ERR_NOT_EXISTS;
	}

	if (!err && par->force_width != 128)
		ResizeIMG (img, false, img, par->force_width, 0);

	return err;
}

//
