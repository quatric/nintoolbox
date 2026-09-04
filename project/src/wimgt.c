
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

#include <sys/stat.h>
#include <unistd.h>

#include "lib-image.h"
#include "lib-nintendo.h"
#include "lib-bntx.h"
#include "lib-gtx.h"
#include "lib-plt0.h"
#include "lib-excite.h"
#include "ui.h" // [[dclib]] wrapper
#include "ui-wimgt.c"

//
///////////////////////////////////////////////////////////////////////////////
///////////////		functions not neded by wimgt		///////////////
///////////////////////////////////////////////////////////////////////////////

#if !SZS_WRAPPER
bool DefineIntVar (VarMap_t *vm, ccp varname, int value)
{
	return false;
}
#endif

//
///////////////////////////////////////////////////////////////////////////////
///////////////			definitions			///////////////
///////////////////////////////////////////////////////////////////////////////

#define TITLE                                                                                      \
	WIMGT_SHORT ": " WIMGT_LONG " v" VERSION " r" REVISION " " SYSTEM2 " - " AUTHOR " - " DATE

//
///////////////////////////////////////////////////////////////////////////////

static void help_exit (bool xmode)
{
	SetupPager ();
	fputs (TITLE "\n", stdout);

	if (xmode)
	{
		int cmd;
		for (cmd = 0; cmd < CMD__N; cmd++)
			PrintHelpCmd (&InfoUI_wimgt, stdout, 0, cmd, 0, 0, URI_HOME);
	}
	else
		PrintHelpCmd (&InfoUI_wimgt, stdout, 0, 0, "HELP", 0, URI_HOME);

	ClosePager ();
	ExitFixed (ERR_OK);
}

///////////////////////////////////////////////////////////////////////////////

static void print_version_section (bool print_sect_header)
{
	cmd_version_section (print_sect_header, WIMGT_SHORT, WIMGT_LONG, long_count - 1);
}

///////////////////////////////////////////////////////////////////////////////

static void version_exit ()
{
	if (brief_count > 1)
		fputs (VERSION "\n", stdout);
	else if (brief_count)
		fputs (VERSION " r" REVISION " " SYSTEM2 "\n", stdout);
	else if (print_sections)
		print_version_section (true);
	else if (long_count)
		print_version_section (false);
	else
		fputs (TITLE "\n", stdout);

	ExitFixed (ERR_OK);
}

///////////////////////////////////////////////////////////////////////////////

static void print_title (FILE *f)
{
	static bool done = false;
	if (!done)
	{
		done = true;
		if (print_sections)
			print_version_section (true);
		else if (verbose >= 1 && f == stdout)
			fprintf (f, "\n%s\n\n", TITLE);
		else
			fprintf (f, "*****  %s  *****\n", TITLE);
	}
}

///////////////////////////////////////////////////////////////////////////////

static const KeywordTab_t *current_command = 0;

static void hint_exit (enumError stat)
{
	if (current_command)
		fprintf (stderr, "-> Type '%s help %s' (pipe it to a pager like 'less') for more help.\n\n",
			ProgInfo.progname, CommandInfo[current_command->id].name1);
	else
		fprintf (stderr,
			"-> Type '%s -h' or '%s help' (pipe it to a pager like 'less') for more help.\n\n",
			ProgInfo.progname, ProgInfo.progname);
	ExitFixed (stat);
}

///////////////////////////////////////////////////////////////////////////////

#define SYNTAX_ERROR syntax_error (__FUNCTION__, __FILE__, __LINE__)

static void syntax_error (ccp func, ccp file, uint line)
{
	if (current_command && current_command->id >= 0 && current_command->id < InfoUI_wimgt.n_cmd)
	{
		const InfoCommand_t *ic = InfoUI_wimgt.cmd_info + current_command->id;
		if (strchr (ic->syntax, '\n'))
		{
			ccp src = ic->syntax;
			char *dest = iobuf;
			while (*src)
				if ((*dest++ = *src++) == '\n')
				{
					*dest++ = ' ';
					*dest++ = ' ';
					*dest++ = ' ';
				}
			*dest = 0;
			PrintErrorFile (func, file, line, 0, 0, ERR_SYNTAX, "Syntax:\n   %s\n", iobuf);
		}
		else
			PrintErrorFile (func, file, line, 0, 0, ERR_SYNTAX, "Syntax: %s\n", ic->syntax);
	}
	else
		PrintErrorFile (func, file, line, 0, 0, ERR_SYNTAX, "Syntax Error!\n");
	ExitFixed (ERR_SYNTAX);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command test			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_test_options ()
{
	printf ("\nOptions (compatibility: %s; format: hex=dec):\n", PrintOptCompatible ());

	printf ("  test:        %16x = %12d\n", testmode, testmode);
	printf ("  verbose:     %16x = %12d\n", verbose, verbose);
	printf ("  width:       %16x = %12d\n", opt_width, opt_width);
	printf ("  escape-char: %16x = %12d\n", escape_char, escape_char);
	printf ("  cmpr-default:         valid=%d, RGB565: %04x %04x, RGB: %06x %06x\n", opt_cmpr_valid,
		be16 (opt_cmpr_def), be16 (opt_cmpr_def + 2), RGB565_to_RGB (be16 (opt_cmpr_def)),
		RGB565_to_RGB (be16 (opt_cmpr_def + 2)));
	printf ("  n-mipmaps:   %16x = %12d\n", opt_n_images - 1, opt_n_images - 1);
	printf ("  max-mipmaps: %16x = %12d\n", opt_max_images - 1, opt_max_images - 1);
	printf ("  mipmap-size: %16x = %12d\n", opt_min_mipmap_size, opt_min_mipmap_size);
	printf ("  mipmaps:     %16x = %12d\n", opt_mipmaps, opt_mipmaps);
	printf ("  fast-mipmaps:%16x = %12d\n", fast_resize_enabled, fast_resize_enabled);

#if defined(TEST) || defined(DEBUG) || HAVE_WIIMM_EXT
	MipmapOptions_t mmo;
	SetupMipmapOptions (&mmo);
	PrintMipmapOptions (stdout, 2, &mmo);
#endif

	if (patch_image_list.used)
	{
		printf ("  image patch list:\n");
		int i;
		for (i = 0; i < patch_image_list.used; i++)
		{
			const FormatFieldItem_t *ffi = patch_image_list.list + i;
			printf ("\t%04x=%s\n", ffi->patch_mode, ffi->key);
		}
	}

	DumpTransformList (stdout, 1, false);
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_test ()
{
#if 1 || !defined(TEST) // test options

	return cmd_test_options ();

#elif 1

	ParamList_t *param;
	for (param = first_param; param; param = param->next)
	{
		NORMALIZE_FILENAME_PARAM (param);

		Image_t img;
		if (!LoadPNG (&img, true, true, param->arg, 0))
		{
			char path[PATH_MAX];
			char *end = StringCopyS (path, sizeof (path), param->arg);
			if (end > path + 4 && !strcasecmp (end - 4, ".png"))
				end -= 4;
			*end = 0;

			StringCopyE (end, path + sizeof (path), "-std.png");
			SavePNG (&img, true, 0, path, 0, 0, false, 0);

			ConvertIMG (&img, false, 0, IMG_X_RGB, PAL_INVALID);
			StringCopyE (end, path + sizeof (path), "-rgb.png");
			SavePNG (&img, true, 0, path, 0, 0, false, 0);

			ConvertIMG (&img, false, 0, IMG_X_GRAY, PAL_INVALID);
			StringCopyE (end, path + sizeof (path), "-gray.png");
			SavePNG (&img, true, 0, path, 0, 0, false, 0);

			ConvertIMG (&img, false, 0, IMG_X_PAL, PAL_INVALID);
			StringCopyE (end, path + sizeof (path), "-pal.png");
			SavePNG (&img, true, 0, path, 0, 0, false, 0);
		}
		ResetIMG (&img);
	}
	return ERR_OK;

#else
	return ERR_OK;
#endif
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command list			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_list (int long_level)
{
	if (long_level > 0)
	{
		RegisterOptionByIndex (&InfoUI_wimgt, OPT_LONG, long_level, false);
		long_count += long_level;
	}

	if (print_header)
		PrintImageHead (0, long_count);

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		char buf[CHECK_FILE_SIZE];
		enumError err = LoadFILE (arg, 0, 0, buf, sizeof (buf), 1, 0, false);
		if (err > ERR_WARNING)
		{
			if (opt_ignore)
				continue;
			memset (buf, 0, sizeof (buf));
		}
		PrintImage (buf, CHECK_FILE_SIZE, arg, 0, 0, long_count, false);
	}

	if (print_header)
		putchar ('\n');

	ResetStringField (&plist);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command decode			///////////////
///////////////////////////////////////////////////////////////////////////////

// BRFNT stores large fonts as a sequence of narrow TGLP texture strips.  A
// strip per PNG is faithful to the container, but awkward for editors.  Join
// the strips left-to-right and write the character-to-rectangle information
// beside the atlas.  Return ERR_NOTHING_TO_DO for non-Wii-font inputs.
static enumError decode_brfnt_atlas (ccp arg, ccp dest)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return err;
	if (raw_size < 0x10 || memcmp (raw, "RFNT", 4) && memcmp (raw, "RFNA", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const uint nsec = be16 (raw + 0x0e);
	const u8 *tglp = 0;
	uint off = 0x10;
	for (uint i = 0; i < nsec && off + 8 <= raw_size; i++)
	{
		const uint len = be32 (raw + off + 4);
		if (len < 8 || len > raw_size - off)
			break;
		if (!memcmp (raw + off, "TGLP", 4) && len >= 0x20)
			tglp = raw + off;
		off += len;
	}
	if (!tglp)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	Image_t img;
	err = LoadIMG (&img, true, arg, 0, false, true, opt_ignore > 0);
	if (err)
	{
		FREE (raw);
		return err;
	}
	const uint nsheet = img.info_n_image;
	const uint sw = img.width, sh = img.height;
	ResetIMG (&img);
	if (!sw || !sh || nsheet > UINT_MAX / sw || sw * nsheet > UINT_MAX / 4 / sh)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}
	const uint atlas_w = sw * nsheet, atlas_h = sh;
	u8 *atlas = CALLOC (1, (size_t)atlas_w * atlas_h * 4);
	if (!atlas)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}

	for (uint sheet = 0; sheet < nsheet; sheet++)
	{
		err = LoadIMG (&img, true, arg, sheet, false, true, opt_ignore > 0);
		if (err)
			break;
		// Transform2XIMG() only flags the pending conversion (RGBA8/CMPR/...
		// -> flat X_RGB); it never touches img.data itself. Without actually
		// running it, the memcpy() below was copying the sheet's raw GX-tiled
		// bytes (AR/GB block pairs for RGBA8) straight into the atlas as if
		// they were already a flat RGBA raster.
		if (Transform2XIMG (&img))
		{
			err = ExecTransformIMG (&img);
			if (err)
				break;
		}
		for (uint y = 0; y < sh; y++)
			memcpy (atlas + ((size_t)y * atlas_w + sheet * sw) * 4,
				img.data + (size_t)y * img.xwidth * 4, (size_t)sw * 4);
		ResetIMG (&img);
	}
	if (err)
	{
		FREE (atlas);
		FREE (raw);
		return err;
	}

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE %s:%s[%u sheets] -> PNG:%s + XML\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			!memcmp (raw, "RFNA", 4) ? "BRFNA" : "BRFNT", arg, nsheet, dest);
	if (testmode)
	{
		FREE (atlas);
		FREE (raw);
		return ERR_OK;
	}
	err = SaveDecodedRGBAToPNG (atlas, atlas_w, atlas_h, &be_func, dest, 0, false);
	if (err)
	{
		FREE (raw);
		return err;
	}

	char xml_path[PATH_MAX];
	ccp ext = strrchr (dest, '.');
	if (ext)
		snprintf (xml_path, sizeof (xml_path), "%.*s.xml", (int)(ext - dest), dest);
	else
		snprintf (xml_path, sizeof (xml_path), "%s.xml", dest);
	File_t F;
	err = CreateFileOpt (&F, true, xml_path, false, arg);
	if (!F.f)
	{
		FREE (raw);
		return err;
	}

	const uint cell_w = tglp[8], cell_h = tglp[9];
	const uint cells_per_row = be16 (tglp + 0x14);
	const uint cell_rows = be16 (tglp + 0x16);
	const uint per_sheet = cells_per_row * cell_rows;
	fprintf (F.f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<font-atlas image=\"%s\" width=\"%u\" height=\"%u\" sheets=\"%u\" "
		"sheet-width=\"%u\" sheet-height=\"%u\">\n", dest, atlas_w, atlas_h, nsheet, sw, sh);

	// Each CMAP maps Unicode/code-page values to glyph indices.  Methods 0,
	// 1 and 2 are direct, table and scan mappings respectively.
	off = 0x10;
	for (uint si = 0; si < nsec && off + 8 <= raw_size; si++)
	{
		const uint len = be32 (raw + off + 4);
		if (len < 8 || len > raw_size - off)
			break;
		const u8 *sec = raw + off;
		if (!memcmp (sec, "CMAP", 4) && len >= 0x14 && per_sheet)
		{
			const uint first = be16 (sec + 8), last = be16 (sec + 10), method = be16 (sec + 12);
			uint count = 0;
			if (method == 2 && len >= 0x16)
				count = be16 (sec + 0x14);
			else if (last >= first)
				count = last - first + 1;
			for (uint j = 0; j < count; j++)
			{
				uint code, glyph;
				if (method == 0 && len >= 0x16)
				{
					code = first + j;
					glyph = be16 (sec + 0x14) + j;
				}
				else if (method == 1 && 0x14 + j * 2 + 2 <= len)
				{
					code = first + j;
					glyph = be16 (sec + 0x14 + j * 2);
				}
				else if (method == 2 && 0x16 + j * 4 + 4 <= len)
				{
					code = be16 (sec + 0x16 + j * 4);
					glyph = be16 (sec + 0x18 + j * 4);
				}
				else
					continue;
				if (glyph == 0xffff || glyph / per_sheet >= nsheet)
					continue;
				const uint sheet = glyph / per_sheet, local = glyph % per_sheet;
				const uint x = sheet * sw + local % cells_per_row * (cell_w + 1);
				const uint y = local / cells_per_row * (cell_h + 1);
				int left = 0;
				uint glyph_w = cell_w, advance = cell_w;
				uint wo = 0x10;
				for (uint wi = 0; wi < nsec && wo + 0x10 <= raw_size; wi++)
				{
					const uint wl = be32 (raw + wo + 4);
					if (wl < 8 || wl > raw_size - wo) break;
					if (!memcmp (raw + wo, "CWDH", 4) && wl >= 0x10)
					{
						const uint wf = be16 (raw + wo + 8), we = be16 (raw + wo + 10);
						if (glyph >= wf && glyph <= we && 0x10 + (glyph-wf)*3 + 3 <= wl)
						{
							const u8 *m = raw + wo + 0x10 + (glyph-wf)*3;
							left = (s8)m[0]; glyph_w = m[1]; advance = m[2]; break;
						}
					}
					wo += wl;
				}
				fprintf (F.f, "  <character code=\"U+%04X\" glyph=\"%u\" sheet=\"%u\" "
					"x=\"%u\" y=\"%u\" width=\"%u\" height=\"%u\" left=\"%d\" "
					"glyph-width=\"%u\" advance=\"%u\"/>\n", code, glyph, sheet,
					x, y, cell_w, cell_h, left, glyph_w, advance);
			}
		}
		off += len;
	}
	fputs ("</font-atlas>\n", F.f);
	if (ferror (F.f) && !err)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing font atlas XML failed: %s\n", xml_path);
	ResetFile (&F, opt_preserve);
	FREE (raw);
	return err;
}

static enumError cmd_decode ()
{
	ccp def_path = "\1P/\1F.png";
	CheckOptDest (def_path, false);

	enumError max_err = ERR_OK;

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		Image_t img;
		enumError err = LoadIMG (&img, true, arg, 0, opt_mipmaps >= 0, true, opt_ignore > 0);
		if (max_err < err)
			max_err = err;
		if (err)
			continue;

		char dest[PATH_MAX];
		SubstDest (dest, sizeof (dest), arg, opt_dest, def_path, 0, false);

		// Wii fonts with multiple TGLP records are more useful as one atlas.
		// The helper also emits the matching character-placement XML.
		err = decode_brfnt_atlas (arg, dest);
		if (err != ERR_NOTHING_TO_DO)
		{
			if (max_err < err)
				max_err = err;
			ResetIMG (&img);
			continue;
		}

		// A TPL's top-level table contains independent textures.  Older wimgt
		// treated those records as a mip chain, which both produced misleading
		// .mmNN names and applied the first texture's geometry to later records.
		// Reload each table record separately and give it a stable image suffix.
		// TGLP glyph sheets, like a TPL top-level table, are independent atlases
		// rather than a mip chain.  Native BRFNT handling leaves the file format
		// unknown intentionally, so recognize its multiple-record metadata here.
		const uint record_images
			= IsTplFF (img.info_fform) || img.info_fform == FF_UNKNOWN && img.info_n_image > 1
			? img.info_n_image
			: 1;
		if (record_images > 1)
		{
			ResetIMG (&img);
			for (uint image_index = 0; image_index < record_images; image_index++)
			{
				err = LoadIMG (&img, true, arg, image_index, false, true, opt_ignore > 0);
				if (max_err < err)
					max_err = err;
				if (err)
					continue;

				char image_dest[PATH_MAX];
				ccp ext = strrchr (dest, '.');
				ccp slash = strrchr (dest, '/');
				if (ext && (!slash || ext > slash))
					snprintf (image_dest, sizeof (image_dest), "%.*s.img%03u%s", (int)(ext - dest),
						dest, image_index, ext);
				else
					snprintf (image_dest, sizeof (image_dest), "%s.img%03u.png", dest, image_index);

				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sDECODE %s:%s[%u] -> PNG:%s\n", verbose > 0 ? "\n" : "",
						testmode ? "WOULD " : "",
						PrintFormat3 (img.info_fform, img.info_iform, img.info_pform), img.path,
						image_index, image_dest);

				TransformIMG (&img, 2);
				if (opt_pre_convert)
				{
					err = ExecTransformIMG (&img);
					if (max_err < err)
						max_err = err;
				}
				if (!testmode)
				{
					err = SavePNG (&img, false, 0, image_dest, 0, 0, false, 0);
					if (max_err < err)
						max_err = err;
				}
				ResetIMG (&img);
			}
			continue;
		}

		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE %s:%s -> PNG:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "",
				PrintFormat3 (img.info_fform, img.info_iform, img.info_pform), img.path, dest);

		TransformIMG (&img, 2);
		if (opt_pre_convert)
		{
			enumError err = ExecTransformIMG (&img);
			if (max_err < err)
				max_err = err;
		}

		if (!testmode)
		{
			err = SavePNG (&img, true, 0, dest, 0, 0, false, 0);
			if (max_err < err)
				max_err = err;
		}
		ResetIMG (&img);
	}

	ResetStringField (&plist);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		    command encode/convert		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError SaveDSB (Image_t *img, ccp dest, ccp source)
{
	Transform2XIMG (img);
	if (img->iform != IMG_X_RGB)
		return ERROR0 (ERR_INVALID_DATA, "Can't convert image to RGBA: %s\n", source);

	u8 *data = 0;
	uint size = 0;
	enumError err = EncodeDSB_RGBA (&data, &size, img->data, img->width, img->height);
	if (err)
		return ERROR0 (ERR_INVALID_DATA, "DSB encoding requires a 128x128 image: %s\n", source);
	File_t F;
	err = CreateFileOpt (&F, true, dest, false, source);
	if (F.f && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err;
}

static enumError SaveFLIM (Image_t *img, ccp dest, ccp source, bool bclim)
{
	Transform2XIMG (img);
	if (img->iform != IMG_X_RGB)
		return ERROR0 (ERR_INVALID_DATA, "Can't convert image to RGBA: %s\n", source);
	const uint rgba_size = img->width * img->height * 4;
	u8 *rgba = MALLOC (rgba_size);
	if (!rgba)
		return ERR_CANT_CREATE;
	for (uint y = 0; y < img->height; y++)
		memcpy (rgba + 4 * y * img->width, img->data + 4 * y * img->xwidth, 4 * img->width);
	u8 *data = 0;
	uint size = 0;
	enumError err = EncodeFLIM_RGBA (&data, &size, rgba, img->width, img->height, bclim);
	FREE (rgba);
	if (err)
		return ERROR0 (
			ERR_INVALID_DATA, "Can't encode %s texture: %s\n", bclim ? "BCLIM" : "BFLIM", source);
	File_t F;
	err = CreateFileOpt (&F, true, dest, false, source);
	if (F.f && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err;
}

static enumError SaveCTPK (Image_t *img, ccp dest, ccp source)
{
	Transform2XIMG (img);
	if (img->iform != IMG_X_RGB)
		return ERROR0 (ERR_INVALID_DATA, "Can't convert image to RGBA: %s\n", source);
	const uint rgba_size = img->width * img->height * 4;
	u8 *rgba = MALLOC (rgba_size);
	if (!rgba)
		return ERR_CANT_CREATE;
	for (uint y = 0; y < img->height; y++)
		memcpy (rgba + 4 * y * img->width, img->data + 4 * y * img->xwidth, 4 * img->width);
	u8 *data = 0;
	uint size = 0;
	enumError err = EncodeCTPK (&data, &size, rgba, img->width, img->height, source);
	FREE (rgba);
	if (err)
		return ERROR0 (ERR_INVALID_DATA, "Can't encode CTPK texture: %s\n", source);
	File_t F;
	err = CreateFileOpt (&F, true, dest, false, source);
	if (F.f && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err;
}

static enumError SaveNCGR (Image_t *img, ccp dest, ccp source)
{
	Transform2XIMG (img);
	if (img->iform != IMG_X_RGB)
		return ERROR0 (ERR_INVALID_DATA, "Can't convert image to RGBA: %s\n", source);
	const uint rgba_size = img->width * img->height * 4;
	u8 *rgba = MALLOC (rgba_size);
	if (!rgba)
		return ERR_CANT_CREATE;
	for (uint y = 0; y < img->height; y++)
		memcpy (rgba + 4 * y * img->width, img->data + 4 * y * img->xwidth, 4 * img->width);
	u8 *data = 0;
	uint size = 0;
	enumError err = EncodeNCGR_RGBA (&data, &size, rgba, img->width, img->height, true);
	FREE (rgba);
	if (err)
		return ERROR0 (ERR_INVALID_DATA, "Can't encode NCGR image: %s\n", source);
	File_t F;
	err = CreateFileOpt (&F, true, dest, false, source);
	if (F.f && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err;
}

static enumError SaveNCLR (Image_t *img, ccp dest, ccp source)
{
	Transform2XIMG (img);
	if (img->iform != IMG_X_RGB)
		return ERROR0 (ERR_INVALID_DATA, "Can't convert image to RGBA: %s\n", source);
	const uint rgba_size = img->width * img->height * 4;
	u8 *rgba = MALLOC (rgba_size);
	if (!rgba)
		return ERR_CANT_CREATE;
	for (uint y = 0; y < img->height; y++)
		memcpy (rgba + 4 * y * img->width, img->data + 4 * y * img->xwidth, 4 * img->width);
	u8 *data = 0;
	uint size = 0;
	enumError err = EncodeNCLR_RGBA (&data, &size, rgba, img->width, img->height);
	FREE (rgba);
	if (err)
		return ERROR0 (ERR_INVALID_DATA, "Can't encode NCLR palette: %s\n", source);
	File_t F;
	err = CreateFileOpt (&F, true, dest, false, source);
	if (F.f && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err;
}

static enumError SaveBNTX (Image_t *img, ccp dest, ccp source)
{
	Transform2XIMG (img);
	if (img->iform != IMG_X_RGB)
		return ERROR0 (ERR_INVALID_DATA, "Can't convert image to RGBA: %s\n", source);
	const uint rgba_size = img->width * img->height * 4;
	u8 *rgba = MALLOC (rgba_size);
	if (!rgba)
		return ERR_CANT_CREATE;
	for (uint y = 0; y < img->height; y++)
		memcpy (rgba + 4 * y * img->width, img->data + 4 * y * img->xwidth, 4 * img->width);
	u8 *data = 0;
	uint size = 0;
	ccp base = strrchr (dest, '/');
	base = base ? base + 1 : dest;
	char name[128];
	snprintf (name, sizeof (name), "%s", base ? base : "texture");
	char *dot = strrchr (name, '.');
	if (dot)
		*dot = 0;
	enumError err = EncodeBNTX_RGBA (&data, &size, rgba, img->width, img->height, name);
	FREE (rgba);
	if (err)
		return ERROR0 (ERR_INVALID_DATA, "Can't encode BNTX image: %s\n", source);
	File_t F;
	err = CreateFileOpt (&F, true, dest, false, source);
	if (F.f && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err;
}

static enumError SaveGTX (Image_t *img, ccp dest, ccp source)
{
	Transform2XIMG (img);
	if (img->iform != IMG_X_RGB)
		return ERROR0 (ERR_INVALID_DATA, "Can't convert image to RGBA: %s\n", source);
	const uint rgba_size = img->width * img->height * 4;
	u8 *rgba = MALLOC (rgba_size);
	if (!rgba)
		return ERR_CANT_CREATE;
	for (uint y = 0; y < img->height; y++)
		memcpy (rgba + 4 * y * img->width, img->data + 4 * y * img->xwidth, 4 * img->width);
	u8 *data = 0;
	uint size = 0;
	enumError err = EncodeGTX_RGBA (&data, &size, rgba, img->width, img->height);
	FREE (rgba);
	if (err)
		return ERROR0 (ERR_INVALID_DATA, "Can't encode GTX image: %s\n", source);
	File_t F;
	err = CreateFileOpt (&F, true, dest, false, source);
	if (F.f && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err;
}

static enumError SavePLT0 (Image_t *img, ccp dest, ccp source)
{
	Transform2XIMG (img);
	if (img->iform != IMG_X_RGB)
		return ERROR0 (ERR_INVALID_DATA, "Can't convert image to RGBA: %s\n", source);
	const uint num_pixels = img->width * img->height;
	if (!num_pixels)
		return ERROR0 (ERR_INVALID_DATA, "Empty image: %s\n", source);

	u8 *data = 0;
	uint size = 0;
	enumError err = ERR_OK;

	if (img->height > 1)
	{
		// 2D image: quantize palette to 256 colors using plt0 3-pass quantization
		u8 *raw_pal = 0;
		uint pal_size = 0, n_colors = 0;
		err = QuantizePalette_PLT0 (img->data, img->width, img->height, img->xwidth,
			img->pform, 256, &raw_pal, &pal_size, &n_colors, 0);
		if (err)
			return ERROR0 (ERR_INVALID_DATA, "Can't quantize PLT0 palette: %s\n", source);

		size = 0x40 + pal_size;
		data = CALLOC (1, size);
		if (!data)
		{
			FREE (raw_pal);
			return ERR_OUT_OF_MEMORY;
		}

		u32 raw_pform = 2; // RGB5A3
		if (img->pform == PAL_IA8)
			raw_pform = 0;
		else if (img->pform == PAL_RGB565)
			raw_pform = 1;

		memcpy (data, "PLT0", 4);
		data[0x04] = (u8)(size >> 24);
		data[0x05] = (u8)(size >> 16);
		data[0x06] = (u8)(size >> 8);
		data[0x07] = (u8)(size & 0xFF);
		data[0x0B] = 1; // version 1
		data[0x13] = 0x40; // headerLen
		data[0x1B] = (u8)raw_pform; // pixelFormat
		data[0x1C] = (u8)(n_colors >> 8);
		data[0x1D] = (u8)(n_colors & 0xFF);
		memcpy (data + 0x40, raw_pal, pal_size);
		FREE (raw_pal);
	}
	else
	{
		// 1-high palette strip
		u8 *rgba = MALLOC (num_pixels * 4);
		if (!rgba)
			return ERR_CANT_CREATE;
		for (uint y = 0; y < img->height; y++)
			memcpy (rgba + 4 * y * img->width, img->data + 4 * y * img->xwidth, 4 * img->width);
		err = EncodePLT0_RGBA (&data, &size, rgba, num_pixels, img->pform);
		FREE (rgba);
	}

	if (err)
		return ERROR0 (ERR_INVALID_DATA, "Can't encode PLT0 palette: %s\n", source);
	File_t F;
	err = CreateFileOpt (&F, true, dest, false, source);
	if (F.f && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err;
}

static enumError SaveBNR (Image_t *img, ccp dest, ccp source)
{
	Transform2XIMG (img);
	if (img->iform != IMG_X_RGB || img->width != 96 || img->height != 32)
		return ERROR0 (ERR_INVALID_DATA, "BNR encoding requires a 96x32 RGBA image: %s\n", source);
	u8 *rgba = MALLOC (96 * 32 * 4);
	if (!rgba)
		return ERR_CANT_CREATE;
	for (uint y = 0; y < 32; y++)
		memcpy (rgba + 4 * y * 96, img->data + 4 * y * img->xwidth, 4 * 96);
	u8 *data = 0;
	uint size = 0;
	enumError err = EncodeBNR_RGBA (&data, &size, rgba, 96, 32);
	FREE (rgba);
	if (err)
		return err;
	File_t F;
	err = CreateFileOpt (&F, true, dest, false, source);
	if (F.f && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err;
}

static enumError SaveBRFNT (Image_t *img, ccp dest, ccp source)
{
	Transform2XIMG (img);
	if (img->iform != IMG_X_RGB)
		return ERROR0 (ERR_INVALID_DATA, "Can't convert image to RGBA: %s\n", source);
	const uint rgba_size = img->width * img->height * 4;
	if (!rgba_size)
		return ERROR0 (ERR_INVALID_DATA, "Empty image: %s\n", source);
	u8 *rgba = MALLOC (rgba_size);
	if (!rgba)
		return ERR_CANT_CREATE;
	for (uint y = 0; y < img->height; y++)
		memcpy (rgba + 4 * y * img->width, img->data + 4 * y * img->xwidth, 4 * img->width);
	u8 *data = 0;
	uint size = 0;
	enumError err = EncodeBRFNT_RGBA (&data, &size, rgba, img->width, img->height, 0, 0);
	FREE (rgba);
	if (err)
		return ERROR0 (ERR_INVALID_DATA, "Can't encode BRFNT font: %s\n", source);
	File_t F;
	err = CreateFileOpt (&F, true, dest, false, source);
	if (F.f && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err;
}

static enumError SaveBRFNA (Image_t *img, ccp dest, ccp source)
{
	Transform2XIMG (img);
	if (img->iform != IMG_X_RGB)
		return ERROR0 (ERR_INVALID_DATA, "Can't convert image to RGBA: %s\n", source);
	const uint rgba_size = img->width * img->height * 4;
	if (!rgba_size)
		return ERROR0 (ERR_INVALID_DATA, "Empty image: %s\n", source);
	u8 *rgba = MALLOC (rgba_size);
	if (!rgba)
		return ERR_CANT_CREATE;
	for (uint y = 0; y < img->height; y++)
		memcpy (rgba + 4 * y * img->width, img->data + 4 * y * img->xwidth, 4 * img->width);
	u8 *data = 0;
	uint size = 0;
	enumError err = EncodeBRFNA_RGBA (&data, &size, rgba, img->width, img->height, 0, 0);
	FREE (rgba);
	if (err)
		return ERROR0 (ERR_INVALID_DATA, "Can't encode BRFNA font: %s\n", source);
	File_t F;
	err = CreateFileOpt (&F, true, dest, false, source);
	if (F.f && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err;
}

static enumError SaveBCFNT (Image_t *img, ccp dest, ccp source, bool is_wiiu)
{
	Transform2XIMG (img);
	if (img->iform != IMG_X_RGB)
		return ERROR0 (ERR_INVALID_DATA, "Can't convert image to RGBA: %s\n", source);
	const uint rgba_size = img->width * img->height * 4;
	if (!rgba_size)
		return ERROR0 (ERR_INVALID_DATA, "Empty image: %s\n", source);
	u8 *rgba = MALLOC (rgba_size);
	if (!rgba)
		return ERR_CANT_CREATE;
	for (uint y = 0; y < img->height; y++)
		memcpy (rgba + 4 * y * img->width, img->data + 4 * y * img->xwidth, 4 * img->width);
	u8 *data = 0;
	uint size = 0;
	enumError err = EncodeBCFNT_RGBA (&data, &size, rgba, img->width, img->height, 0, 0, is_wiiu);
	FREE (rgba);
	if (err)
		return ERROR0 (
			ERR_INVALID_DATA, "Can't encode %s font: %s\n", is_wiiu ? "BFFNT" : "BCFNT", source);
	File_t F;
	err = CreateFileOpt (&F, true, dest, false, source);
	if (F.f && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err;
}

static enumError SaveExciteTEX (Image_t *img, ccp dest, ccp source, bool headered)
{
	Transform2XIMG (img);
	if (img->iform != IMG_X_RGB)
		return ERROR0 (ERR_INVALID_DATA, "Can't convert image to RGBA: %s\n", source);
	const uint rgba_size = img->width * img->height * 4;
	if (!rgba_size)
		return ERROR0 (ERR_INVALID_DATA, "Empty image: %s\n", source);
	u8 *rgba = MALLOC (rgba_size);
	if (!rgba)
		return ERR_CANT_CREATE;
	for (uint y = 0; y < img->height; y++)
		memcpy (rgba + 4 * y * img->width, img->data + 4 * y * img->xwidth, 4 * img->width);
	u8 *data = 0;
	uint size = 0;
	enumError err = headered
		? EncodeExciteTEXHeader_RGBA (&data, &size, rgba, img->width, img->height, -1)
		: EncodeExciteTEX_RGBA (&data, &size, rgba, img->width, img->height, -1);
	FREE (rgba);
	if (err)
		return ERROR0 (ERR_INVALID_DATA,
			"Can't encode Excite TEX: width/height must be a power of two (4..1024),\n"
			"or no pixel format could be found that round-trips correctly for this\n"
			"image's dimensions: %s\n",
			source);
	File_t F;
	err = CreateFileOpt (&F, true, dest, false, source);
	if (F.f && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err;
}

static enumError SaveExciteART (Image_t *img, ccp dest, ccp source, bool headered)
{
	Transform2XIMG (img);
	if (img->iform != IMG_X_RGB)
		return ERROR0 (ERR_INVALID_DATA, "Can't convert image to RGBA: %s\n", source);
	const uint rgba_size = img->width * img->height * 4;
	if (!rgba_size)
		return ERROR0 (ERR_INVALID_DATA, "Empty image: %s\n", source);
	u8 *rgba = MALLOC (rgba_size);
	if (!rgba)
		return ERR_CANT_CREATE;
	for (uint y = 0; y < img->height; y++)
		memcpy (rgba + 4 * y * img->width, img->data + 4 * y * img->xwidth, 4 * img->width);
	u8 *data = 0;
	uint size = 0;
	enumError err = headered
		? EncodeExciteARTHeader_RGBA (&data, &size, rgba, img->width, img->height, -1)
		: EncodeExciteART_RGBA (&data, &size, rgba, img->width, img->height, -1);
	FREE (rgba);
	if (err)
		return ERROR0 (ERR_INVALID_DATA,
			"Can't encode Excite ART/IMG: width/height must be a power of two (4..1024),\n"
			"or no pixel format could be found that round-trips correctly for this\n"
			"image's dimensions: %s\n",
			source);
	File_t F;
	err = CreateFileOpt (&F, true, dest, false, source);
	if (F.f && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err;
}

static enumError cmd_convert (int cmd_id, ccp cmd_name, ccp def_path)
{
	CheckOptDest (def_path, false);

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		Image_t img;
		enumError err = LoadIMG (&img, true, arg, 0, opt_mipmaps >= 0, false, opt_ignore);
		if (err == ERR_NOT_EXISTS || err == ERR_WARNING)
			continue;
		if (err > ERR_WARNING)
			return err;

		const file_format_t src_f = img.info_fform;
		const image_format_t src_i = img.info_iform;
		const palette_format_t src_p = img.info_pform;
		TransformIMG (&img, -1);

		char dest[PATH_MAX];
		SubstDest (dest, sizeof (dest), arg, opt_dest, def_path, 0, false);

		const ccp dot = strrchr (dest, '.');
		if (dot && !strcasecmp (dot, ".dsb"))
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%s%s %s:%s -> DSB:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", cmd_name, PrintFormat3 (src_f, src_i, src_p), arg,
					dest);
			if (!testmode)
				err = SaveDSB (&img, dest, arg);
			ResetIMG (&img);
			if (err > ERR_WARNING)
				return err;
			continue;
		}
		if (dot && (!strcasecmp (dot, ".bflim") || !strcasecmp (dot, ".bclim")))
		{
			const bool bclim = !strcasecmp (dot, ".bclim");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%s%s %s:%s -> %s:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", cmd_name, PrintFormat3 (src_f, src_i, src_p), arg,
					bclim ? "BCLIM" : "BFLIM", dest);
			if (!testmode)
				err = SaveFLIM (&img, dest, arg, bclim);
			ResetIMG (&img);
			if (err > ERR_WARNING)
				return err;
			continue;
		}
		if (dot && !strcasecmp (dot, ".ctpk"))
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%s%s %s:%s -> CTPK:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", cmd_name, PrintFormat3 (src_f, src_i, src_p), arg,
					dest);
			if (!testmode)
				err = SaveCTPK (&img, dest, arg);
			ResetIMG (&img);
			if (err > ERR_WARNING)
				return err;
			continue;
		}
		if (dot && !strcasecmp (dot, ".ncgr"))
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%s%s %s:%s -> NCGR:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", cmd_name, PrintFormat3 (src_f, src_i, src_p), arg,
					dest);
			if (!testmode)
				err = SaveNCGR (&img, dest, arg);
			ResetIMG (&img);
			if (err > ERR_WARNING)
				return err;
			continue;
		}
		if (dot && !strcasecmp (dot, ".nclr"))
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%s%s %s:%s -> NCLR:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", cmd_name, PrintFormat3 (src_f, src_i, src_p), arg,
					dest);
			if (!testmode)
				err = SaveNCLR (&img, dest, arg);
			ResetIMG (&img);
			if (err > ERR_WARNING)
				return err;
			continue;
		}
		if (dot && !strcasecmp (dot, ".bntx"))
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%s%s %s:%s -> BNTX:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", cmd_name, PrintFormat3 (src_f, src_i, src_p), arg,
					dest);
			if (!testmode)
				err = SaveBNTX (&img, dest, arg);
			ResetIMG (&img);
			if (err > ERR_WARNING)
				return err;
			continue;
		}
		if (dot && !strcasecmp (dot, ".gtx"))
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%s%s %s:%s -> GTX:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", cmd_name, PrintFormat3 (src_f, src_i, src_p), arg,
					dest);
			if (!testmode)
				err = SaveGTX (&img, dest, arg);
			ResetIMG (&img);
			if (err > ERR_WARNING)
				return err;
			continue;
		}
		if (dot && !strcasecmp (dot, ".plt0"))
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%s%s %s:%s -> PLT0:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", cmd_name, PrintFormat3 (src_f, src_i, src_p), arg,
					dest);
			if (!testmode)
				err = SavePLT0 (&img, dest, arg);
			ResetIMG (&img);
			if (err > ERR_WARNING)
				return err;
			continue;
		}
		if (dot && !strcasecmp (dot, ".bnr"))
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%s%s %s:%s -> BNR:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", cmd_name, PrintFormat3 (src_f, src_i, src_p), arg,
					dest);
			if (!testmode)
				err = SaveBNR (&img, dest, arg);
			ResetIMG (&img);
			if (err > ERR_WARNING)
				return err;
			continue;
		}
		if (dot && (!strcasecmp (dot, ".brfnt") || !strcasecmp (dot, ".rfnt")))
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%s%s %s:%s -> BRFNT:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", cmd_name, PrintFormat3 (src_f, src_i, src_p), arg,
					dest);
			if (!testmode)
				err = SaveBRFNT (&img, dest, arg);
			ResetIMG (&img);
			if (err > ERR_WARNING)
				return err;
			continue;
		}
		if (dot && (!strcasecmp (dot, ".brfna") || !strcasecmp (dot, ".rfna")))
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%s%s %s:%s -> BRFNA:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", cmd_name, PrintFormat3 (src_f, src_i, src_p), arg,
					dest);
			if (!testmode)
				err = SaveBRFNA (&img, dest, arg);
			ResetIMG (&img);
			if (err > ERR_WARNING)
				return err;
			continue;
		}
		if (dot && (!strcasecmp (dot, ".bcfnt") || !strcasecmp (dot, ".cfnt")))
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%s%s %s:%s -> BCFNT:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", cmd_name, PrintFormat3 (src_f, src_i, src_p), arg,
					dest);
			if (!testmode)
				err = SaveBCFNT (&img, dest, arg, false);
			ResetIMG (&img);
			if (err > ERR_WARNING)
				return err;
			continue;
		}
		if (dot && (!strcasecmp (dot, ".bffnt") || !strcasecmp (dot, ".ffnt")))
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%s%s %s:%s -> BFFNT:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", cmd_name, PrintFormat3 (src_f, src_i, src_p), arg,
					dest);
			if (!testmode)
				err = SaveBCFNT (&img, dest, arg, true);
			ResetIMG (&img);
			if (err > ERR_WARNING)
				return err;
			continue;
		}
		// Not plain ".tex": that extension is already claimed by the BRRES
		// TEX0 encoder above (SaveTEX(), reached via the generic fform path
		// below). ".etex" avoids the collision; rename the result to ".tex"
		// afterwards to use it with wszst or the game.
		if (dot && (!strcasecmp (dot, ".etex") || !strcasecmp (dot, ".ebtex")))
		{
			const bool headered = !strcasecmp (dot, ".ebtex");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%s%s %s:%s -> Excite-TEX:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", cmd_name, PrintFormat3 (src_f, src_i, src_p), arg,
					dest);
			if (!testmode)
				err = SaveExciteTEX (&img, dest, arg, headered);
			ResetIMG (&img);
			if (err > ERR_WARNING)
				return err;
			continue;
		}
		if (dot && (!strcasecmp (dot, ".art") || !strcasecmp (dot, ".img")
				|| !strcasecmp (dot, ".ebart") || !strcasecmp (dot, ".ebimg")))
		{
			const bool headered
				= !strcasecmp (dot, ".ebart") || !strcasecmp (dot, ".ebimg");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%s%s %s:%s -> Excite-ART:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", cmd_name, PrintFormat3 (src_f, src_i, src_p), arg,
					dest);
			if (!testmode)
				err = SaveExciteART (&img, dest, arg, headered);
			ResetIMG (&img);
			if (err > ERR_WARNING)
				return err;
			continue;
		}
		const file_format_t fform = GetImageFF (img.tform_valid ? img.tform_fform : FF_INVALID,
			FF_INVALID, dest, img.info_fform, cmd_id == CMD_CONVERT, FF_TEX);

		if (fform == FF_PNG)
			Transform2XIMG (&img);
		else
			Transform2InternIMG (&img);

		const bool samefile = !strcmp (arg, dest);
		if (!all_count && !img.conv_count && !img.tform_exec && !img.patch_done
			&& fform == img.info_fform && samefile)
		{
			continue;
		}

		if (verbose >= 0 || testmode)
		{
			image_format_t iform
				= img.tform_valid && img.tform_iform != IMG_INVALID ? img.tform_iform : img.iform;
			palette_format_t pform
				= img.tform_valid && img.tform_pform != PAL_INVALID ? img.tform_pform : img.pform;

			fprintf (stdlog, "%s%s%s %s:%s -> %s:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", cmd_name, PrintFormat3 (src_f, src_i, src_p), arg,
				PrintFormat3 (fform, iform, pform), dest);
		}

		if (opt_pre_convert)
			TransformIMG (&img, 2);

		if (!testmode)
		{
			err = SaveIMG (&img, fform, 0, 0, dest, samefile);
			if (err > ERR_WARNING)
				return err;
		}
		ResetIMG (&img);
	}

	ResetStringField (&plist);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command copy			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_copy ()
{
	RegisterOptionByIndex (&InfoUI_wimgt, OPT_OVERWRITE, 1, false);
	opt_overwrite = true;

	if (!opt_dest)
	{
		if (!first_param)
			return ERROR0 (ERR_MISSING_PARAM, "Missing destination parameter!\n");

		ParamList_t *param;
		for (param = first_param; param->next; param = param->next)
			;
		ASSERT (param);
		ASSERT (!param->next);
		SetDest (param->arg, false);
		param->arg = 0;
	}

	file_format_t fform_dest = FF_UNKNOWN;
	ccp point = strrchr (opt_dest, '.');
	if (point)
		fform_dest = IsImageFF (GetByNameFF (point + 1), true);
	PRINT ("fform(dest) = %s\n", GetNameFF (0, fform_dest));

	int done_count = 0;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];
		done_count++;

		Image_t img;
		enumError err = LoadIMG (&img, true, arg, 0, opt_mipmaps >= 0, false, opt_ignore);
		if (err == ERR_NOT_EXISTS || err == ERR_WARNING)
			continue;
		if (err > ERR_WARNING)
			return err;

		const file_format_t src_f = img.info_fform;
		const image_format_t src_i = img.info_iform;
		const palette_format_t src_p = img.info_pform;
		TransformIMG (&img, -1);

		file_format_t fform = img.tform_valid && img.tform_fform != FF_INVALID ? img.tform_fform
			: fform_dest != FF_UNKNOWN										   ? fform_dest
																			   : src_f;
		PRINT0 ("FFORM = %s\n", GetNameFF (0, fform));

		char dest[PATH_MAX];
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, 0, false);

		if (fform == FF_PNG)
			Transform2XIMG (&img);
		else
			Transform2InternIMG (&img);

		const bool samefile = !strcmp (arg, dest);
		if (!all_count && !img.conv_count && !img.tform_exec && !img.patch_done
			&& fform == img.info_fform && samefile)
		{
			continue;
		}

		const bool create_tpl = IsTplFF (fform);
		if (!create_tpl && (verbose >= 0 || testmode))
		{
			image_format_t iform
				= img.tform_valid && img.tform_iform != IMG_INVALID ? img.tform_iform : img.iform;
			palette_format_t pform
				= img.tform_valid && img.tform_pform != PAL_INVALID ? img.tform_pform : img.pform;

			fprintf (stdlog, "%s%sCOPY %s:%s -> %s:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", PrintFormat3 (src_f, src_i, src_p), arg,
				PrintFormat3 (fform, iform, pform), dest);
		}

		if (opt_pre_convert)
			TransformIMG (&img, 2);

		tpl_raw_t raw;
		if (create_tpl)
		{
			err = CreateRawTPL (&raw, &img, fform);

			if (verbose >= 0 || testmode)
			{
				fform = IsTplHeaderEx (raw.data.ptr, raw.data.len) ? FF_TPLX : FF_TPL;
				fprintf (stdlog, "%s%sCOPY %s:%s -> %s:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", PrintFormat3 (src_f, src_i, src_p), arg,
					PrintFormat3 (fform, img.iform, img.pform), dest);
			}
		}

		if (!testmode)
		{
			err = create_tpl ? SaveRawTPL (&raw, 0, dest, samefile)
							 : SaveIMG (&img, fform, 0, 0, dest, samefile);
			;
			if (err > ERR_WARNING)
				return err;
		}
		if (create_tpl)
			ResetRawTPL (&raw);
		ResetIMG (&img);
	}

	if (!done_count)
		SYNTAX_ERROR;

	ResetStringField (&plist);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////                   check options                 ///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError CheckOptions (int argc, char **argv, bool is_env)
{
	TRACE ("CheckOptions(%d,%p,%d) optind=%d\n", argc, argv, is_env, optind);

	optind = 0;
	int err = 0;

	for (;;)
	{
		const int opt_stat = getopt_long (argc, argv, OptionShort, OptionLong, 0);
		if (opt_stat == -1)
			break;

		RegisterOptionByName (&InfoUI_wimgt, opt_stat, 1, is_env);

		switch ((enumGetOpt)opt_stat)
		{
			case GO__ERR:
				err++;
				break;

			case GO_VERSION:
				version_exit ();
			case GO_HELP:
				help_exit (false);
			case GO_XHELP:
				help_exit (true);
			case GO_CONFIG:
				opt_config = optarg;
			case GO_YDEBUG:
				enable_ydebug++;
				break;
			case GO_ALLOW_ALL:
				allow_all = true;
				break;
			case GO_COMPATIBLE:
				err += ScanOptCompatible (optarg);
				break;
			case GO_WIDTH:
				err += ScanOptWidth (optarg);
				break;
			case GO_MAX_WIDTH:
				err += ScanOptMaxWidth (optarg);
				break;
			case GO_NO_PAGER:
				opt_no_pager = true;
				break;
			case GO_ZERO:
				opt_zero++;
				break;
			case GO_QUIET:
				verbose = verbose > -1 ? -1 : verbose - 1;
				break;
			case GO_VERBOSE:
				verbose = verbose < 0 ? 0 : verbose + 1;
				break;
			case GO_LOGGING:
				logging++;
				break;
			case GO_EXT_ERRORS:
				ext_errors++;
				break;
			case GO_TIMING:
				log_timing++;
				break;
			case GO_WARN:
				err += ScanOptWarn (optarg);
				break;
			case GO_DE:
				use_de = true;
				break;
			case GO_CT_CODE:
				ctcode_enabled = true;
				break;
			case GO_LE_CODE:
				lecode_enabled = true;
				break; // optional argument ignored
			case GO_LE_04X:
				lecode_04x = true;
				break;
			case GO_COLORS:
				err += ScanOptColorize (0, optarg, 0);
				break;
			case GO_NO_COLORS:
				opt_colorize = COLMD_OFF;
				break;

			case GO_CHDIR:
				err += ScanOptChdir (optarg);
				break;
			case GO_CONST:
				break; // [[2do]] err += ScanOptConst(optarg); break;

			case GO_UTF_8:
				use_utf8 = true;
				break;
			case GO_NO_UTF_8:
				use_utf8 = false;
				break;

			case GO_TEST:
				testmode++;
				break;
			case GO_FORCE:
				force_count++;
				break;
			case GO_REPAIR_MAGICS:
				err += ScanOptRepairMagic (optarg);
				break;

#if OPT_OLD_NEW
			case GO_OLD:
				opt_new = opt_new > 0 ? -1 : opt_new - 1;
				break;
			case GO_STD:
				opt_new = 0;
				break;
			case GO_NEW:
				opt_new = opt_new < 0 ? +1 : opt_new + 1;
				break;
#endif
			case GO_EXTRACT:
				opt_extract = optarg;
				break;

			case GO_ESC:
				err += ScanEscapeChar (optarg) < 0;
				break;
			case GO_DEST:
				SetDest (optarg, false);
				break;
			case GO_DEST2:
				SetDest (optarg, true);
				break;
			case GO_OVERWRITE:
				opt_overwrite = true;
				break;
			case GO_NUMBER:
				opt_number = true;
				break;
			case GO_REMOVE_DEST:
				opt_remove_dest = true;
				break;
			case GO_UPDATE:
				opt_update = true;
				break;
			case GO_PRESERVE:
				opt_preserve = true;
				break;
			case GO_IGNORE:
				opt_ignore++;
				break;

			case GO_MAX_FILE_SIZE:
				err += ScanOptMaxFileSize (optarg);
				break;
			case GO_TRACKS:
				err += ScanOptTracks (optarg);
				break;
			case GO_ARENAS:
				err += ScanOptArenas (optarg);
				break;

			case GO_LONG:
				long_count++;
				break;
			case GO_NO_HEADER:
				print_header = false;
				break;
			case GO_BRIEF:
				brief_count++;
				break;
			case GO_NO_WILDCARDS:
				no_wildcards_count++;
				break;
			case GO_IN_ORDER:
				inorder_count++;
				break;
			case GO_SECTIONS:
				print_sections++;
				break;

			case GO_ALL:
				all_count++;
				break;
			case GO_CMPR_DEFAULT:
				err += ScanOptCmprDefault (optarg);
				break;
			case GO_N_MIPMAPS:
				err += ScanOptNMipmaps (optarg);
				break;
			case GO_MAX_MIPMAPS:
				err += ScanOptMaxMipmaps (optarg);
				break;
			case GO_MIPMAP_SIZE:
				err += ScanOptMipmapSize (optarg);
				break;
			case GO_MIPMAPS:
				opt_mipmaps = +1;
				break;
			case GO_NO_MIPMAPS:
				opt_mipmaps = -1;
				break;
			case GO_FAST_MIPMAPS:
				fast_resize_enabled = true;
				break;
			case GO_PRE_CONVERT:
				opt_pre_convert = true;
				break;
			case GO_PATCH:
				err += ScanOptPatchImage (optarg);
				break;
			case GO_TRANSFORM:
				err += ScanOptTransform (optarg);
				break;
			case GO_STRIP:
				opt_strip = true;
				break;

				// no default case defined
				//	=> compiler checks the existence of all enum values
		}
	}

#ifdef DEBUG
	DumpUsedOptions (&InfoUI_wimgt, TRACE_FILE, 11);
#endif
	NormalizeOptions (verbose > 3 && !is_env);

	return !err ? ERR_OK : ProgInfo.max_error ? ProgInfo.max_error : ERR_SYNTAX;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////                   check command                 ///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError CheckCommand (int argc, char **argv)
{
	const KeywordTab_t *cmd_ct = CheckCommandHelper (argc, argv, CommandTab);
	if (!cmd_ct)
		hint_exit (ERR_SYNTAX);

	TRACE ("COMMAND FOUND: #%lld = %s\n", (u64)cmd_ct->id, cmd_ct->name1);
	current_command = cmd_ct;

	if (!allow_all)
	{
		enumError err = VerifySpecificOptions (&InfoUI_wimgt, cmd_ct);
		if (err)
			hint_exit (err);
	}
	WarnDepractedOptions (&InfoUI_wimgt);

	if (cmd_ct->id != CMD_ARGTEST)
	{
		argc -= optind + 1;
		argv += optind + 1;

		if (cmd_ct->id == CMD_TEST)
			while (argc-- > 0)
				AddParam (*argv++);
		else
			while (argc-- > 0)
				AtFileHelper (*argv++, AddParam);
	}

	enumError err = ERR_OK;
	switch ((enumCommands)cmd_ct->id)
	{
		case CMD_VERSION:
			version_exit ();
		case CMD_HELP:
			PrintHelpColor (&InfoUI_wimgt);
			break;
		case CMD_CONFIG:
			err = cmd_config ();
			break;
		case CMD_ARGTEST:
			err = cmd_argtest (argc, argv);
			break;
		case CMD_EXPAND:
			err = cmd_expand (argc, argv);
			break;
		case CMD_TEST:
			err = cmd_test ();
			break;
		case CMD_COLORS:
			err = Command_COLORS (brief_count ? -brief_count : long_count, 0, 0);
			break;
		case CMD_ERROR:
			err = cmd_error ();
			break;
		case CMD_FILETYPE:
			err = cmd_filetype ();
			break;
		case CMD_FILEATTRIB:
			err = cmd_fileattrib ();
			break;

		case CMD_LIST:
			err = cmd_list (0);
			break;
		case CMD_LIST_L:
			err = cmd_list (1);
			break;
		case CMD_LIST_LL:
			err = cmd_list (2);
			break;

		case CMD_DECODE:
			err = cmd_decode ();
			break;
		case CMD_ENCODE:
			err = cmd_convert (cmd_ct->id, "ENCODE", "\1P/\1N");
			break;
		case CMD_CONVERT:
			err = cmd_convert (cmd_ct->id, "CONVERT", 0);
			break;
		case CMD_COPY:
			err = cmd_copy ();
			break;

			// no default case defined
			//	=> compiler checks the existence of all enum values

		case CMD__NONE:
		case CMD__N:
			help_exit (false);
	}
	ResetPatchListIMG ();
	return PrintErrorStat (err, verbose, cmd_ct->name1);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			   main()			///////////////
///////////////////////////////////////////////////////////////////////////////

#if SZS_WRAPPER
int main_wimgt (int argc, char **argv)
#else
int main (int argc, char **argv)
#endif
{
#if !SZS_WRAPPER
	ArgManager_t am = { 0 };
	SetupArgManager (&am, LOUP_AUTO, argc, argv, false);
	ExpandAtArgManager (&am, AMXM_SHORT, 10, false);
	argc = am.argc;
	argv = am.argv;
#endif

	tool_name = "wimgt";
	print_title_func = print_title;
	SetupLib (argc, argv, WIMGT_SHORT, VERSION, TITLE);

	//----- process arguments

	if (argc < 2)
	{
		printf ("\n%s\n%s\nVisit %s%s for more info.\n\n", text_logo, TITLE, URI_HOME, WIMGT_SHORT);
		hint_exit (ERR_OK);
	}

	enumError err = CheckEnvOptions2 ("WIMGT_OPT", CheckOptions);
	if (err)
		hint_exit (err);

	err = CheckOptions (argc, argv, false);
	if (err)
		hint_exit (err);

	err = CheckCommand (argc, argv);
	DUMP_TRACE_ALLOC (TRACE_FILE);

	if (SIGINT_level)
		err = ERROR0 (ERR_INTERRUPT, "Program interrupted by user.");
	ClosePager ();
	return FixExitStatus (err);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    END				///////////////
///////////////////////////////////////////////////////////////////////////////
