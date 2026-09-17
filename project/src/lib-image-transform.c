
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

///////////////////////////////////////////////////////////////////////////////
///////////////			transformation data		///////////////
///////////////////////////////////////////////////////////////////////////////
// [[transform_mode_t]]

typedef enum transform_mode_t
{
	TM_IDX_FILE, // file format (=byte index)
	TM_IDX_IMG, // image format (=byte index)
	TM_IDX_PAL, // palette format (=byte index)
	TM_IDX_PALETTE, // switch PALETTE
	TM_IDX_COLOR, // switch COLOR
	TM_IDX_ALPHA, // switch ALPHA

	TM_IDX_N, // number of modes
	TM_IDX_MASK = 7,

	TM_F_PAL = 0x10, // flag: Palette support

	TF_ON = 1,
	TF_OFF = 2,
} transform_mode_t;

///////////////////////////////////////////////////////////////////////////////
// [[transform_t]]

typedef struct transform_t
{
	char src[TM_IDX_N];
	char dest[TM_IDX_N];
} transform_t;

#define MAX_TRANSFORM 100

uint n_transform = 0;
transform_t transform[MAX_TRANSFORM];

//
///////////////		    scan file and image format		///////////////
///////////////////////////////////////////////////////////////////////////////

const KeywordTab_t cmdtab_transform[] = { //--- file formats

	// [[tpl-ex+]]
	{ FF_TPL, "TPL", 0, TM_IDX_FILE | TM_F_PAL },
	{ FF_TPLX, "TPLx", "TPLX", TM_IDX_FILE | TM_F_PAL },
	{ FF_CUPICON, "CUPICON", "CUPICONS", TM_IDX_FILE }, { FF_CUPICON, "CUP", "CUPS", TM_IDX_FILE },
	{ FF_BTI, "BTI", 0, TM_IDX_FILE | TM_F_PAL }, { FF_TEX, "TEX", "TEX0", TM_IDX_FILE },
	{ FF_BREFT_IMG, "BREFT-IMG", "BREFTIMG", TM_IDX_FILE },
	{ FF_BREFT_IMG, "REFT-IMG", "REFTIMG", TM_IDX_FILE },
	{ FF_BREFT_IMG, "BT-IMG", "BTIMG", TM_IDX_FILE }, { FF_PNG, "PNG", 0, TM_IDX_FILE },
	{ FF_AJPG, "AJPG", 0, TM_IDX_FILE }, { FF_CTXB, "CTXB", 0, TM_IDX_FILE },
	{ FF_NFTR, "NFTR", 0, TM_IDX_FILE }, { FF_BCFNT, "BCFNT", 0, TM_IDX_FILE },

	//--- image formats

	{ IMG_I4, "I4", 0, TM_IDX_IMG }, { IMG_I8, "I8", 0, TM_IDX_IMG },
	{ IMG_IA4, "IA4", 0, TM_IDX_IMG }, { IMG_IA8, "IA8", 0, TM_IDX_IMG },
	{ IMG_RGB565, "RGB565", "R565", TM_IDX_IMG }, { IMG_RGB5A3, "RGB5A3", "R3", TM_IDX_IMG },
	{ IMG_RGBA32, "RGBA32", "RGBA8", TM_IDX_IMG }, { IMG_RGBA32, "R32", "R8", TM_IDX_IMG },
	{ IMG_C4, "C4", "CI4", TM_IDX_IMG | TM_F_PAL }, { IMG_C8, "C8", "CI8", TM_IDX_IMG | TM_F_PAL },
	{ IMG_C14X2, "C14X2", "CI14X2", TM_IDX_IMG | TM_F_PAL }, { IMG_CMPR, "CMPR", 0, TM_IDX_IMG },

	//--- palette formats

	{ PAL_IA8, "PIA8", "P-IA8", TM_IDX_PAL }, { PAL_IA8, "P8", "P-8", TM_IDX_PAL },
	{ PAL_RGB565, "PRGB565", "P-RGB565", TM_IDX_PAL }, { PAL_RGB565, "P565", "P-565", TM_IDX_PAL },
	{ PAL_RGB5A3, "PRGB5A3", "P-RGB5A3", TM_IDX_PAL }, { PAL_RGB5A3, "P3", "P-3", TM_IDX_PAL },

	//--- switch PALETTE

	{ TF_ON, "PALETTE", 0, TM_IDX_PALETTE }, { TF_OFF, "-PALETTE", "NOPALETTE", TM_IDX_PALETTE },

	//--- switch COLOR

	{ TF_ON, "COLOR", 0, TM_IDX_COLOR }, { TF_OFF, "GRAY", "GREY", TM_IDX_COLOR },

	//--- switch ALPHA

	{ TF_ON, "ALPHA", 0, TM_IDX_ALPHA }, { TF_OFF, "-ALPHA", "NOALPHA", TM_IDX_ALPHA },

	//--- end of table

	{ 0, 0, 0, 0 }
};

///////////////////////////////////////////////////////////////////////////////
// [[transform_term_t]]

typedef struct transform_term_t
{
	ccp arg;
	char res[TM_IDX_N];
	uint opt[TM_IDX_N];
} transform_term_t;

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

static enumError ScanTransformKeyword (transform_term_t *term)
{
	DASSERT (term);
	ccp name = term->arg;

	while (*name > 0 && *name <= ' ')
		name++;

	char namebuf[20], *end = namebuf + sizeof (namebuf) - 1, *dest = namebuf;
	while (*name >= '0' && *name <= '9' || *name >= 'a' && *name <= 'z'
		|| *name >= 'A' && *name <= 'Z' || *name == '-')
	{
		if (dest < end)
			*dest++ = *name;
		name++;
	}
	while (*name > 0 && *name <= ' ')
		name++;
	term->arg = name;

	if (dest == namebuf)
		return ERR_OK;
	*dest = 0;

	const KeywordTab_t *cmd = ScanKeyword (0, namebuf, cmdtab_transform);
	if (!cmd)
		return ERROR0 (ERR_SYNTAX, "Invalid keyword for option --transform: %s\n", namebuf);

	uint idx = cmd->opt & TM_IDX_MASK;
	DASSERT (idx < TM_IDX_N);
	term->res[idx] = cmd->id;
	term->opt[idx] = cmd->opt;

	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError ScanTransformTerm (transform_term_t *term, ccp arg)
{
	DASSERT (term);
	memset (term, 0, sizeof (*term));
	memset (term->res, -1, sizeof (term->res));
	if (!arg)
		return ERR_OK;

	for (;;)
	{
		while (*arg > 0 && *arg <= ' ' || *arg == '.')
			arg++;
		term->arg = arg;
		enumError err = ScanTransformKeyword (term);
		if (err)
			return err;
		arg = term->arg;
		if (*arg != '.')
			return ERR_OK;
	}
}

///////////////////////////////////////////////////////////////////////////////

int ScanOptTransform (ccp arg)
{
	n_transform = 0;
	if (!arg)
		return 0;

	for (;;)
	{
		while (*arg > 0 && *arg <= ' ' || *arg == ',')
			arg++;
		if (!*arg)
			break;

		ccp src = 0, dest = arg;
		while (*arg && *arg != ',' && *arg != '=')
			arg++;
		if (*arg == '=')
		{
			src = dest;
			arg++;
			while (*arg > 0 && *arg <= ' ')
				arg++;
			dest = arg;
			while (*arg && *arg != ',' && *arg != '=')
				arg++;
		}

		if (!src && arg == dest)
			goto err_abort;

		transform_term_t dterm;
		enumError err = ScanTransformTerm (&dterm, dest);
		if (err)
			return err;
		if (dterm.arg != arg)
		{
			arg = dterm.arg;
			goto err_abort;
		}
		PRINT0 ("DEST: %d,%d,%d [%u,%u,%u]\n", dterm.res[0], dterm.res[1], dterm.res[2],
			dterm.opt[0], dterm.opt[1], dterm.opt[2]);

		for (;;)
		{
			transform_term_t sterm;
			enumError err = ScanTransformTerm (&sterm, src);
			if (err)
				return err;
			PRINT0 ("SRC: %d,%d,%d [%u,%u,%u]\n", sterm.res[0], sterm.res[1], sterm.res[2],
				sterm.opt[0], sterm.opt[1], sterm.opt[2]);

			if (n_transform == MAX_TRANSFORM)
			{
				ERROR0 (ERR_SYNTAX, "Option --transform: Only %u terms allowed!\n", MAX_TRANSFORM);
				return 1;
			}

			transform_t *t = transform + n_transform++;
			memcpy (t->src, sterm.res, sizeof (t->src));
			memcpy (t->dest, dterm.res, sizeof (t->dest));

			// special case: destionation is FF_CUPICON
			if (t->src[TM_IDX_FILE] == FF_CUPICON)
				t->src[TM_IDX_FILE] = FF_TPLX;
			if (t->dest[TM_IDX_FILE] == FF_CUPICON)
				t->dest[TM_IDX_IMG] = IMG_CMPR;

			src = sterm.arg;
			if (!src)
				break;
			if (*src == '+')
				src++;
			else if (*src == '=')
				break;
			else
			{
				arg = src;
				goto err_abort;
			}
		}
	}
	return 0;

err_abort:
	ERROR0 (ERR_SYNTAX, "Invalid parameter for option --transform: %.20s\n", arg);
	return 1;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

static ccp GetTransformName (uint index, int mode, ccp not_found_text)
{
	if (mode != -1)
	{
		const KeywordTab_t *ct;
		for (ct = cmdtab_transform; ct->name1; ct++)
			if (ct->id == mode && (ct->opt & TM_IDX_MASK) == index)
				return ct->name1;
	}
	return not_found_text;
}

///////////////////////////////////////////////////////////////////////////////

void DumpTransformList (FILE *f, int indent, bool force)
{
	DASSERT (f);
	if (!n_transform)
		return;

	fprintf (f,
		"\n"
		"%*s file        source formats             -> file     destination formats\n"
		"%*s type    image  palette pal  color alph -> type    image  palette pal  color alph\n"
		"%*s----------------------------------------------------------------------------------\n",
		indent, "", indent, "", indent, "");
	uint i;
	for (i = 0; i < n_transform; i++)
	{
		const transform_t *t = transform + i;
		fprintf (f,
			"%*s %-7s %-6s %-7s %-4.4s %-5s %-4.4s"
			" -> %-7s %-6s %-7s %-4.4s %-5s %-4.4s\n",
			indent, "", GetTransformName (0, t->src[0], "*"), GetTransformName (1, t->src[1], "*"),
			GetTransformName (2, t->src[2], "*"), GetTransformName (3, t->src[3], "*"),
			GetTransformName (4, t->src[4], "*"), GetTransformName (5, t->src[5], "*"),
			GetTransformName (0, t->dest[0], "*"), GetTransformName (1, t->dest[1], "*"),
			GetTransformName (2, t->dest[2], "*"), GetTransformName (3, t->dest[3], "*"),
			GetTransformName (4, t->dest[4], "*"), GetTransformName (5, t->dest[5], "*"));
	}
	fprintf (f, "\n");
}

///////////////////////////////////////////////////////////////////////////////

ccp PrintTransformTuple (ccp tuple)
{
	const uint bufsize = 50;
	char *buf = GetCircBuf (bufsize);
	char *dest = buf, *bufend = buf + bufsize - TM_IDX_N;

	uint idx;
	for (idx = 0; idx < TM_IDX_N; idx++)
	{
		if (tuple[idx] != -1)
		{
			ccp name = GetTransformName (idx, tuple[idx], 0);
			if (name)
			{
				*dest++ = '.';
				dest = StringCopyE (dest, bufend++, name);
			}
		}
	}
	*dest = 0;
	return dest == buf ? "*" : buf + 1;
}

///////////////////////////////////////////////////////////////////////////////

ccp PrintFormat3 (file_format_t fform, // file format
	image_format_t iform, // image format
	palette_format_t pform // palette format
)
{
	// Unlike transform_t's src/dest (always a small nintendo-format id or a
	// TF_ON/OFF flag, so 'char' is fine there), fform/iform here can be any
	// registered format constant -- e.g. FF_NFTR=160 or an IMG_X_* value
	// (>=0x7c00) for a non-nintendo decode. Routing those through
	// PrintTransformTuple()'s 'char tuple[]' truncated them to one byte,
	// silently aliasing e.g. IMG_X_RGB (0x7c02) to the unrelated registered
	// IMG_IA4 (0x02) and printing a wrong label instead of the real one.
	const uint bufsize = 50;
	char *buf = GetCircBuf (bufsize);
	char *dest = buf, *bufend = buf + bufsize - 3;

	const file_format_t file_id = fform == FF_BREFT ? FF_BREFT_IMG : fform;
	ccp name = GetTransformName (TM_IDX_FILE, file_id, 0);
	if (name)
	{
		*dest++ = '.';
		dest = StringCopyE (dest, bufend++, name);
	}

	if (fform != FF_PNG)
	{
		name = GetTransformName (TM_IDX_IMG, iform, 0);
		if (name)
		{
			*dest++ = '.';
			dest = StringCopyE (dest, bufend++, name);
		}
		if (GetPaletteCountIF (iform))
		{
			name = GetTransformName (TM_IDX_PAL, pform, 0);
			if (name)
			{
				*dest++ = '.';
				dest = StringCopyE (dest, bufend++, name);
			}
		}
	}

	*dest = 0;
	return dest == buf ? "*" : buf + 1;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		    transformation functions		///////////////
///////////////////////////////////////////////////////////////////////////////

void SetupTransformIMG (Image_t *img)
{
	DASSERT (img);

	if (!img->tform_valid)
	{
		img->tform_valid = true;
		img->tform_exec = false;
		img->tform_gray = false;
		img->tform_noalpha = false;
		img->tform_fform0 = img->tform_fform = FF_INVALID;
		img->tform_iform0 = img->tform_iform = img->iform;
		img->tform_pform0 = img->tform_pform = img->pform;
	}
}

///////////////////////////////////////////////////////////////////////////////

bool Transform2InternIMG (Image_t *img)
{
	SetupTransformIMG (img);

	switch (img->tform_iform)
	{
		case IMG_X_GRAY:
			// Prefer the alpha-less format automatically if the source has
			// no real alpha data: it gives more precision for the same size
			// instead of wasting bits on an alpha channel nobody set.
			img->tform_iform
				= img->tform_noalpha || CheckAlphaIMG (img, false) < 0 ? IMG_I8 : IMG_IA4;
			return img->tform_exec = true;

		case IMG_X_RGB:
			img->tform_iform
				= img->tform_noalpha || CheckAlphaIMG (img, false) < 0 ? IMG_RGB565 : IMG_RGB5A3;
			return img->tform_exec = true;

		case IMG_X_PAL4:
			img->tform_iform = IMG_C4;
			return img->tform_exec = true;

		case IMG_X_PAL8:
			img->tform_iform = IMG_C8;
			return img->tform_exec = true;

		case IMG_X_PAL:
		case IMG_X_PAL14:
			img->tform_iform = IMG_C14X2;
			return img->tform_exec = true;

		default:
			return false;
	}
}

///////////////////////////////////////////////////////////////////////////////

bool Transform2XIMG (Image_t *img)
{
	SetupTransformIMG (img);

	switch (img->tform_iform)
	{
		case IMG_I4:
		case IMG_I8:
		case IMG_IA4:
		case IMG_IA8:
			img->tform_iform = IMG_X_GRAY;
			return img->tform_exec = true;

		case IMG_RGB565:
		case IMG_RGB5A3:
		case IMG_RGBA32:
		case IMG_CMPR:
			img->tform_iform = IMG_X_RGB;
			return img->tform_exec = true;

		case IMG_C4:
			img->tform_iform = IMG_X_PAL4;
			return img->tform_exec = true;

		case IMG_C8:
			img->tform_iform = IMG_X_PAL8;
			return img->tform_exec = true;

		case IMG_C14X2:
			img->tform_iform = IMG_X_PAL14;
			return img->tform_exec = true;

		default:
			return false;
	}
}

///////////////////////////////////////////////////////////////////////////////

bool Transform2XRGB (Image_t *img)
{
	SetupTransformIMG (img);
	if (img->tform_iform == IMG_X_RGB)
		return false;

	img->tform_iform = IMG_X_RGB;
	return img->tform_exec = true;
}

///////////////////////////////////////////////////////////////////////////////

bool Transform2PaletteIMG (Image_t *img)
{
	SetupTransformIMG (img);
	noPRINT ("Transform2PaletteIMG(%s)\n", GetImageFormatName (img->tform_iform, "?"));

	switch (img->tform_iform)
	{
		case IMG_I4:
			img->tform_iform = IMG_C4;
			img->tform_pform = PAL_IA8;
			return img->tform_exec = true;

		case IMG_I8:
		case IMG_IA4:
		case IMG_IA8:
			img->tform_iform = IMG_C8;
			img->tform_pform = PAL_IA8;
			return img->tform_exec = true;

		case IMG_RGB565:
			img->tform_iform = IMG_C8;
			img->tform_pform = PAL_RGB565;
			return img->tform_exec = true;

		case IMG_RGB5A3:
		case IMG_CMPR:
			img->tform_iform = IMG_C8;
			img->tform_pform = img->tform_noalpha ? PAL_RGB565 : PAL_RGB5A3;
			return img->tform_exec = true;

		case IMG_RGBA32:
			img->tform_iform = IMG_C14X2;
			img->tform_pform = img->tform_noalpha ? PAL_RGB565 : PAL_RGB5A3;
			return img->tform_exec = true;

		case IMG_X_GRAY:
		case IMG_X_RGB:
			img->tform_iform = IMG_X_PAL;
			img->tform_pform = PAL_X_RGB;
			return img->tform_exec = true;

		default:
			return false;
	}
}

///////////////////////////////////////////////////////////////////////////////

bool Transform2NoPaletteIMG (Image_t *img)
{
	SetupTransformIMG (img);

	switch (img->tform_iform)
	{
		case IMG_C4:
		case IMG_C8:
		case IMG_C14X2:
			img->tform_iform = PaletteToImageFormat (img->tform_pform, IMG_X_RGB);
			return img->tform_exec = true;

		case IMG_X_PAL4:
		case IMG_X_PAL8:
		case IMG_X_PAL14:
		case IMG_X_PAL:
			img->tform_iform = IMG_X_RGB;
			return img->tform_exec = true;

		default:
			return false;
	}
}

///////////////////////////////////////////////////////////////////////////////

bool Transform2GrayIMG (Image_t *img)
{
	SetupTransformIMG (img);
	PRINT ("TRANSFORM GRAY: %s\n", PrintFormat3 (0, img->tform_iform, img->tform_pform));

	switch (img->tform_iform)
	{
		case IMG_RGB565:
		case IMG_RGB5A3:
		case IMG_CMPR:
			img->tform_gray = true;
			img->tform_iform = IMG_I8;
			return img->tform_exec = true;

		case IMG_RGBA32:
			img->tform_gray = true;
			img->tform_iform = img->tform_noalpha ? IMG_I8 : IMG_IA8;
			return img->tform_exec = true;

		case IMG_C4:
		case IMG_C8:
		case IMG_C14X2:
			img->tform_gray = true;
			img->tform_pform = PAL_IA8;
			return img->tform_exec = true;

		case IMG_X_RGB:
		case IMG_X_PAL4:
		case IMG_X_PAL8:
		case IMG_X_PAL14:
		case IMG_X_PAL:
			img->tform_gray = true;
			img->tform_iform = IMG_X_GRAY;
			return img->tform_exec = true;

		default:
			return false;
	}
}

///////////////////////////////////////////////////////////////////////////////

bool Transform2ColorIMG (Image_t *img)
{
	SetupTransformIMG (img);

	switch (img->tform_iform)
	{
		case IMG_I4:
		case IMG_I8:
			img->tform_iform = IMG_RGB565;
			return img->tform_exec = true;

		case IMG_IA4:
		case IMG_IA8:
			img->tform_iform = IMG_RGB5A3;
			return img->tform_exec = true;

		case IMG_C4:
		case IMG_C8:
		case IMG_C14X2:
			if (img->tform_pform != PAL_RGB565)
				img->tform_pform = PAL_RGB5A3;
			return img->tform_exec = true;

		case IMG_X_GRAY:
			img->tform_iform = IMG_X_RGB;
			return img->tform_exec = true;

		default:
			return false;
	}
}

///////////////////////////////////////////////////////////////////////////////

bool Transform2AlphaIMG (Image_t *img)
{
	SetupTransformIMG (img);

	switch (img->tform_iform)
	{
		case IMG_I4:
			img->tform_iform = IMG_IA4;
			return img->tform_exec = true;

		case IMG_I8:
			img->tform_iform = IMG_IA8;
			return img->tform_exec = true;

		case IMG_RGB565:
			img->tform_iform = IMG_RGB5A3;
			return img->tform_exec = true;

		case IMG_C4:
		case IMG_C8:
		case IMG_C14X2:
			if (img->tform_pform != PAL_IA8)
				img->tform_pform = PAL_RGB5A3;
			return img->tform_exec = true;

		default:
			return false;
	}
}

///////////////////////////////////////////////////////////////////////////////

bool Transform2NoAlphaIMG (Image_t *img)
{
	SetupTransformIMG (img);

	switch (img->tform_iform)
	{
		case IMG_IA4:
			img->tform_iform = IMG_I4;
			img->tform_noalpha = true;
			return img->tform_exec = true;

		case IMG_IA8:
			img->tform_iform = IMG_I8;
			img->tform_noalpha = true;
			return img->tform_exec = true;

		case IMG_RGB5A3:
		case IMG_RGBA32:
			img->tform_iform = IMG_RGB565;
			img->tform_noalpha = true;
			return img->tform_exec = true;

		case IMG_C4:
		case IMG_C8:
		case IMG_C14X2:
			if (img->tform_pform != PAL_IA8)
				img->tform_iform = IMG_RGB565;
			img->tform_noalpha = true;
			return img->tform_exec = true;

		case IMG_X_GRAY:
		case IMG_X_RGB:
		case IMG_X_PAL4:
		case IMG_X_PAL8:
		case IMG_X_PAL14:
		case IMG_X_PAL:
			img->tform_noalpha = true;
			return img->tform_exec = true;

		default:
			return false;
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

bool TransformIMG (
	// The image is NOT converted, only the planned transformations
	// are calculated. Use ExecTransformIMG() to execute transformation
	//  -> returns TRUE if a rule match

	Image_t *img, // destination of transforming
	int indent // <0:  no logging
			   // >=0: log transform with indent
)
{
	SetupTransformIMG (img);

	uint i;
	for (i = 0; i < n_transform; i++)
	{
		const transform_t *t = transform + i;
		ccp s = t->src;
		if (s[TM_IDX_FILE] != FF_INVALID && s[TM_IDX_FILE] != img->tform_fform0
			|| s[TM_IDX_IMG] != FF_INVALID && s[TM_IDX_IMG] != img->tform_iform0
			|| s[TM_IDX_PAL] != FF_INVALID && s[TM_IDX_PAL] != img->tform_pform0)
		{
			continue;
		}

		PRINT ("TFORM-1: %s -> %s\n", PrintTransformTuple (t->src), PrintTransformTuple (t->dest));

		const int palette = s[TM_IDX_PALETTE];
		if (palette > 0)
		{
			if ((palette == TF_OFF) == (GetPaletteCountIF (img->iform) != 0))
				continue;
		}

		const int color = s[TM_IDX_COLOR];
		if (color > 0)
		{
			if ((color == TF_ON) == IsGrayIMG (img))
				continue;
		}

		const int alpha = s[TM_IDX_ALPHA];
		if (alpha > 0)
		{
			if ((alpha == TF_ON ? -1 : 1) == CheckAlphaIMG (img, false))
				continue;
		}

		noPRINT (
			"TFORM-2: %s -> %s\n", PrintTransformTuple (t->src), PrintTransformTuple (t->dest));

		if (indent >= 0)
		{
			printf ("%*s- Transform: %s -> %s\n", indent, "", PrintTransformTuple (t->src),
				PrintTransformTuple (t->dest));
		}

		if (t->dest[TM_IDX_PALETTE] == TF_ON)
			Transform2PaletteIMG (img);
		else if (t->dest[TM_IDX_PALETTE] == TF_OFF)
			Transform2NoPaletteIMG (img);

		if (t->dest[TM_IDX_COLOR] == TF_ON)
			Transform2ColorIMG (img);
		else if (t->dest[TM_IDX_COLOR] == TF_OFF)
			Transform2GrayIMG (img);

		if (t->dest[TM_IDX_ALPHA] == TF_ON)
			Transform2AlphaIMG (img);
		else if (t->dest[TM_IDX_ALPHA] == TF_OFF)
			Transform2NoAlphaIMG (img);

		if (t->dest[TM_IDX_FILE] != FF_INVALID)
			img->tform_fform = t->dest[TM_IDX_FILE];

		if (t->dest[TM_IDX_IMG] != IMG_INVALID)
			img->tform_iform = t->dest[TM_IDX_IMG];

		if (t->dest[TM_IDX_PAL] != PAL_INVALID)
			img->tform_pform = t->dest[TM_IDX_PAL];

		return img->tform_exec = true;
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

bool Transform3IMG (Image_t *img, // valid image
	file_format_t fform, // file format
	image_format_t iform, // image format
	palette_format_t pform, // palette format
	bool def_base // define *form values as base
)
{
	SetupTransformIMG (img);
	bool stat = false;

	fform = IsImageFF (fform, true);
	if (fform != FF_UNKNOWN && img->tform_fform != fform)
	{
		stat = true; // do not set img->tform_exec
		img->tform_fform = fform;
	}

	if (iform != IMG_INVALID && img->tform_iform != iform)
	{
		img->tform_exec = stat = true;
		img->tform_iform = iform;
	}

	if (pform != PAL_INVALID && img->tform_pform != pform)
	{
		img->tform_exec = stat = true;
		img->tform_pform = pform;
	}

	if (def_base)
	{
		img->tform_fform0 = fform;
		img->tform_iform0 = iform;
		img->tform_pform0 = pform;
	}

	return stat;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

enumError ExecTransformIMG (Image_t *img // image to transform
)
{
	DASSERT (img);

	enumError err = ERR_OK;
	if (!img->tform_valid || !img->tform_exec)
		goto abort;

	PRINT ("ExecTransformIMG() %s -> %s\n", PrintFormat3 (0, img->iform, img->pform),
		PrintFormat3 (0, img->tform_iform, img->tform_pform));

	if (img->tform_gray && !img->is_grayed)
	{
		err = ConvertIMG (img, false, 0, IMG_X_GRAY, PAL_AUTO);
		if (err)
			goto abort;
	}

	if (img->tform_noalpha && CheckAlphaIMG (img, false) >= 0)
	{
		err = ConvertIMG (img, false, 0, IMG_X_AUTO, PAL_AUTO);
		if (err)
			goto abort;

		switch (img->iform)
		{
			case IMG_X_GRAY:
			{
				uint n = img->xwidth * img->xheight;
				u8 *data = img->data + 1;
				while (n-- > 0)
				{
					*data = 0xff;
					data += 2;
				}
			}
			break;

			case IMG_X_RGB:
			{
				uint n = img->xwidth * img->xheight;
				u8 *data = img->data + 3;
				while (n-- > 0)
				{
					*data = 0xff;
					data += 4;
				}
			}
			break;

			case IMG_X_PAL:
			case IMG_X_PAL4:
			case IMG_X_PAL8:
			case IMG_X_PAL14:
				if (img->n_pal)
				{
					DASSERT (img->pal);
					uint n = img->n_pal;
					u8 *data = img->data + 1;
					while (n-- > 0)
					{
						*data = 0xff;
						data += 2;
					}
				}
				break;

			default:
				return ERROR0 (ERR_INTERNAL, 0);
		}
		img->alpha_status = -1;
	}

	err = ConvertIMG (img, false, 0, img->tform_iform, img->tform_pform);

abort:
	img->tform_valid = false;
	return err;
}

//
