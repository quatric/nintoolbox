
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

#include <dirent.h>
#include <unistd.h>

#include "lib-mdl.h"
#include "lib-szs.h"
#include "lib-model-glb.h"
#include "lib-hsf.h"
#include "lib-hsd.h"
#include "lib-excite.h"
#include "lib-glg.h"
#include "lib-image.h"
#include "lib-brres.h"
#include "lib-brres-model.h"
#include "lib-brres-inject.h"
#include "lib-nsbmd.h"
#include "lib-bcres.h"
#include "lib-bch.h"
#include "lib-bfres.h"
#include "lib-nud.h"
#include "lib-bnfm.h"
#include "lib-numsh.h"
#include "ui.h" // [[dclib]] wrapper
#include "ui-wmdlt.c"

static ccp opt_parent = 0;

static inline bool is_ext (ccp src, ccp ext)
{
	if (!src || !ext)
		return false;
	const size_t slen = strlen (src);
	const size_t elen = strlen (ext);
	return slen >= elen && !strcasecmp (src + slen - elen, ext);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			definitions			///////////////
///////////////////////////////////////////////////////////////////////////////

#define TITLE                                                                                      \
	WMDLT_SHORT ": " WMDLT_LONG " v" VERSION " r" REVISION " " SYSTEM2 " - " AUTHOR " - " DATE

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
			PrintHelpCmd (&InfoUI_wmdlt, stdout, 0, cmd, 0, 0, URI_HOME);
	}
	else
		PrintHelpCmd (&InfoUI_wmdlt, stdout, 0, 0, "HELP", 0, URI_HOME);

	ClosePager ();
	ExitFixed (ERR_OK);
}

///////////////////////////////////////////////////////////////////////////////

static void print_version_section (bool print_sect_header)
{
	cmd_version_section (print_sect_header, WMDLT_SHORT, WMDLT_LONG, long_count - 1);
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

	printf ("  mdl modes:   %16x = \"%s\"\n", MDL_MODE, GetMdlMode ());
	printf ("  patch files: %16x = \"%s\"\n", PATCH_FILE_MODE, GetFileClassInfo ());
	DumpTransformationOpt ();

	if (opt_tracks)
		DumpTrackList (0, 0, 0);
	if (opt_arenas)
		DumpArenaList (0, 0, 0);

	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_test ()
{
#if 1 || !defined(TEST) // test options

	return cmd_test_options ();

#elif 0

	ParamList_t *param;
	for (param = first_param; param; param = param->next)
	{
		// NORMALIZE_FILENAME_PARAM(param);
	}
	return ERR_OK;

#endif
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command export			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_export ()
{
	SetupVarsMDL ();
	return ExportHelper ("mdl");
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command cat			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError iter_cat (mdl_t *mdl, // MDL data structure
	void *param // a user defined parameter
)
{
	DASSERT (mdl);

	if (verbose >= 0 || testmode)
	{
		fprintf (stdlog, "%sCAT %s:%s\n", verbose > 0 ? "\n" : "", GetNameFF (mdl->fform, 0),
			mdl->fname);
		fflush (stdlog);
	}

	if (!testmode)
	{
		const enumError err = SaveTextMDL (mdl, "-", false);
		if (err > ERR_WARNING)
			return err;
	}
	fflush (stdout);

	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_cat ()
{
	stdlog = stderr;

	raw_data_t raw;
	InitializeRawData (&raw);

	enumError cmd_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];
		enumError err = LoadRawData (&raw, false, arg, 0, opt_ignore > 0, 0);
		if (err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore)
			continue;
		if (err > ERR_WARNING)
			return err;
#if 1
		IterateRawDataMDL (&raw, global_check_mode, iter_cat, 0);
#else
		if (verbose >= 0 || testmode)
		{
			fprintf (stdlog, "%sCAT %s:%s\n", verbose > 0 ? "\n" : "", GetNameFF (raw.fform, 0),
				raw.fname);
			fflush (stdlog);
		}

		mdl_t mdl;
		err = ScanRawDataMDL (&mdl, true, &raw, global_check_mode);
		if (err > ERR_WARNING)
			return err;

		if (!testmode)
		{
			err = SaveTextMDL (&mdl, "-", false);
			if (err > ERR_WARNING)
				return err;
		}
		ResetMDL (&mdl);
#endif
	}

	ResetStringField (&plist);
	ResetRawData (&raw);
	return cmd_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////	  BRRES/archive -> GLB helper (DECODE)			///////////////
///////////////////////////////////////////////////////////////////////////////

// `wmdlt DECODE some.brres --dest out.glb` used to hand the whole archive's
// raw bytes straight to ScanRawDataMDL()/ParseMDL0(), both of which only
// recognize a bare MDL0 file (FF_MDL) at offset 0 -- so it failed every
// single BRRES with "No MDL file", even though the archive plainly contains
// one (or more) MDL0 sub-files, exactly like `wszst xx` extracts and
// converts correctly via its own separate export_models_tree() path. Walk
// the archive here the same way and export each MDL0 found; the common case
// (exactly one model) writes straight to the requested --dest, and the rare
// multi-model BRRES gets one file per model, disambiguated by its NW4R name,
// instead of silently discarding every model past the first.
typedef struct mdl0_export_ctx_t
{
	ccp dest;
	uint count;
	enumError err;

} mdl0_export_ctx_t;

static int iter_export_mdl0_glb (struct szs_iterator_t *it, bool term)
{
	if (term || it->is_dir)
		return 0;

	mdl0_export_ctx_t *ctx = it->param;
	const u8 *data = it->szs->data + it->off;
	// [[analyse-magic]]
	if (GetByMagicFF (data, it->size, it->size) != FF_MDL)
		return 0;

	// A BRRES keeps every sub-file's names in one pool shared across the
	// whole archive, so the raw MDL0 slice carries no strings of its own --
	// each of its texture-name offsets points past the end of the slice.
	// Parsing it in place therefore produced materials with no texture names
	// at all, and every model exported untextured. wszst's own extractor
	// avoids this by rebuilding a per-sub-file pool and appending it
	// (see lib-szs-create.c's CollectStringsBRSUB() call), which is exactly
	// why an extracted .mdl0 exports textured while this path did not.
	// Rebuild the same self-contained buffer before parsing.
	u8 *owned = 0;
	uint owned_size = 0;
	if (it->szs->fform_arch == FF_BRRES)
	{
		szs_file_t sub;
		InitializeSubSZS (&sub, it->szs, it->off, it->size, FF_UNKNOWN, it->path, false);
		string_pool_t sp;
		CollectStringsBRSUB (&sp, true, &sub, true);
		if (sp.size)
		{
			owned_size = sub.size + sp.size;
			owned = MALLOC (owned_size);
			if (owned)
			{
				memcpy (owned, sub.data, sub.size);
				memcpy (owned + sub.size, sp.data, sp.size);
			}
			else
				owned_size = 0;
		}
		ResetStringPool (&sp);
		ResetSZS (&sub);
	}

	model_t *model
		= owned ? ParseMDL0 (owned, owned_size) : ParseMDL0 (data, it->size);
	FREE (owned);
	if (!model)
	{
		ctx->err = ERR_INVALID_DATA;
		return 0;
	}

	char path[PATH_MAX];
	if (!ctx->count)
		snprintf (path, sizeof (path), "%s", ctx->dest);
	else
	{
		char base[PATH_MAX];
		snprintf (base, sizeof (base), "%s", ctx->dest);
		char *dot = strrchr (base, '.');
		if (dot)
			*dot = 0;
		ccp name = *it->path ? it->path : "model";
		ccp slash = strrchr (name, '/');
		if (slash)
			name = slash + 1;
		snprintf (path, sizeof (path), "%s.%s.glb", base, name);
	}

	if (verbose >= 0)
		fprintf (stdlog, "%sEXPORT MODEL:%s -> GLB:%s\n", verbose > 0 ? "\n" : "",
			*it->path ? it->path : ctx->dest, path);

	ExportModelToGLB (model, path);
	FreeModel (model);
	ctx->count++;
	return 0;
}

// A BRRES carries its own textures in Textures(NW4R), but the GLB writer
// resolves them purely by file path: it looks for "<name>.png" beside the
// model or in the tree SetDAETextureSearchRoot() has indexed. Under
// `wszst xx` that tree exists, because extraction has already written every
// TEX0 out as a PNG -- but `wmdlt DECODE some.brres` writes a single GLB
// with nothing beside it, so every material came out untextured even though
// the archive it was read from held the textures all along.
//
// Decode this archive's own TEX0 sub-files into a scratch directory and
// point the search root at it, so a standalone BRRES exports the same
// self-contained GLB (images embedded as bufferViews) that the archive
// walker produces.
// Textures staged beside the model, tracked so cleanup removes exactly the
// files this export created and never a pre-existing one.
typedef struct staged_tex_t
{
	char **paths;
	uint used, size;

} staged_tex_t;

static void staged_tex_add (staged_tex_t *st, ccp path)
{
	if (st->used == st->size)
	{
		const uint next = st->size ? st->size * 2 : 16;
		void *mem = REALLOC (st->paths, next * sizeof (*st->paths));
		if (!mem)
			return;
		st->paths = mem;
		st->size = next;
	}
	st->paths[st->used] = STRDUP (path);
	if (st->paths[st->used])
		st->used++;
}

static void staged_tex_cleanup (staged_tex_t *st)
{
	for (uint i = 0; i < st->used; i++)
	{
		unlink (st->paths[i]);
		FREE (st->paths[i]);
	}
	FREE (st->paths);
	st->paths = 0;
	st->used = st->size = 0;
}

typedef struct tex0_dump_ctx_t
{
	ccp dir;
	staged_tex_t *staged;
	uint count;

} tex0_dump_ctx_t;

static int iter_dump_tex0_png (struct szs_iterator_t *it, bool term)
{
	if (term || it->is_dir)
		return 0;

	tex0_dump_ctx_t *ctx = it->param;
	const u8 *data = it->szs->data + it->off;
	// [[analyse-magic]]
	if (GetByMagicFF (data, it->size, it->size) != FF_TEX)
		return 0;

	ccp name = *it->path ? it->path : "texture";
	ccp slash = strrchr (name, '/');
	if (slash)
		name = slash + 1;
	if (!*name)
		return 0;

	char png_path[PATH_MAX];
	snprintf (png_path, sizeof (png_path), "%s/%s.png", ctx->dir, name);

	// Never clobber a PNG the user already had sitting there -- and never
	// delete it later either, since it is not ours to remove.
	struct stat st_png;
	if (!stat (png_path, &st_png))
	{
		ctx->count++; // still a resolvable texture, just not one we wrote
		return 0;
	}

	Image_t img;
	const enumError aerr = AssignIMG (&img, 1, data, it->size, 0, false, &be_func, name);
	if (!aerr && SaveIMG (&img, FF_PNG, 0, 0, png_path, true) == ERR_OK)
	{
		staged_tex_add (ctx->staged, png_path);
		ctx->count++;
	}
	ResetIMG (&img);
	return 0;
}

// Returns true when 'raw' was handled here (an archive, whether or not it
// actually contained a model -- callers must not fall through to the
// bare-MDL0 path below either way).
static bool export_mdl0_from_archive (raw_data_t *raw, ccp dest, enumError *err)
{
	if (!IsArchiveFF (raw->fform))
		return false;

	szs_file_t szs;
	AssignSZS (&szs, true, raw->data, raw->data_size, false, raw->fform, raw->fname);

	// Stage the archive's textures first: the exporter reads the search
	// root while it writes each model, so it has to be populated up front.
	// They go in the model's own directory rather than a subdirectory,
	// because dae_shared_texture_scope() only accepts a texture that shares
	// the model's directory or sits alongside it under a common root -- a
	// subdirectory below a model at the root is rejected.
	char model_dir[PATH_MAX];
	ccp dslash = strrchr (dest, '/');
	if (dslash)
		snprintf (model_dir, sizeof (model_dir), "%.*s", (int)(dslash - dest), dest);
	else
		snprintf (model_dir, sizeof (model_dir), ".");

	// No SetDAETextureSearchRoot() here on purpose. With the search index
	// enabled dae_texture_path() *skips* any texture it cannot place inside
	// the indexed tree; with it disabled the same function falls back to the
	// bare "<name>.png", which the GLB writer then opens relative to the
	// model it is writing. Staging the PNGs beside that model is therefore
	// all this path needs, and it avoids the scope rules that exist for
	// recursive archive extraction.
	staged_tex_t staged = { 0, 0, 0 };
	tex0_dump_ctx_t tex_ctx = { model_dir, &staged, 0 };
	IterateFilesParSZS (
		&szs, iter_dump_tex0_png, &tex_ctx, false, false, false, -1, -1, SORT_NONE);

	mdl0_export_ctx_t ctx = { dest, 0, ERR_OK };
	IterateFilesParSZS (&szs, iter_export_mdl0_glb, &ctx, false, false, false, -1, -1, SORT_NONE);
	ResetSZS (&szs);

	// The staged PNGs are an implementation detail of this export: the
	// images they carried are embedded in the GLB by now.
	staged_tex_cleanup (&staged);

	*err = ctx.count ? ctx.err : ERROR0 (ERR_INVALID_DATA, "No MDL file: %s\n", raw->fname);
	return true;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////		  command encode/decode			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_convert (int cmd_id, ccp cmd_name, ccp def_path)
{
	CheckOptDest (def_path, false);

	raw_data_t raw;
	InitializeRawData (&raw);

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];
		enumError err = LoadRawData (&raw, false, arg, 0, opt_ignore > 0, 0);
		if (err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore)
			continue;
		if (err > ERR_WARNING)
			return err;

		char dest[PATH_MAX];
		const file_format_t dest_ff = cmd_id == CMD_ENCODE ? FF_MDL : FF_MDL_TXT;

		SubstDest (dest, sizeof (dest), arg, opt_dest, def_path, GetExtFF (dest_ff, 0), false);

		if (verbose >= 0 || testmode)
		{
			fprintf (stdlog, "%s%s%s %s:%s -> %s:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", cmd_name, GetNameFF (raw.fform, 0), raw.fname,
				GetNameFF (dest_ff, 0), dest);
			fflush (stdlog);
		}

		const int dest_len = strlen (dest);
		const bool is_dae = dest_len > 4 && !strcasecmp (dest + dest_len - 4, ".dae");
		const bool is_glb = dest_len > 4 && !strcasecmp (dest + dest_len - 4, ".glb");
		const bool is_hsf = dest_len > 4 && !strcasecmp (dest + dest_len - 4, ".hsf");
		const bool is_hsd = dest_len > 4 && !strcasecmp (dest + dest_len - 4, ".dat");
		const bool is_msh = dest_len > 4 && !strcasecmp (dest + dest_len - 4, ".msh");
		const bool is_mod = dest_len > 4 && !strcasecmp (dest + dest_len - 4, ".mod");
		const bool is_glg = dest_len > 4 && (!strcasecmp (dest + dest_len - 4, ".glg") || !strcasecmp (dest + dest_len - 4, ".rlg"));
		const bool is_bfres = dest_len > 6 && !strcasecmp (dest + dest_len - 6, ".bfres");
		const bool is_nud = dest_len > 4 && !strcasecmp (dest + dest_len - 4, ".nud");
		const bool is_bnfm = dest_len > 5 && !strcasecmp (dest + dest_len - 5, ".bnfm");
		const bool is_model_dest = is_dae || is_glb;

		const int arg_len = strlen (arg);
		const bool is_glb_input = (arg_len > 4 && !strcasecmp (arg + arg_len - 4, ".glb"))
			|| (raw.data_size > 12 && !memcmp (raw.data, "glTF", 4));
		const bool is_dae_input = (arg_len > 4 && !strcasecmp (arg + arg_len - 4, ".dae"))
			|| (raw.data_size > 10 && strstr ((const char *)raw.data, "<COLLADA"));
		if (is_dae_input || is_glb_input)
		{
			if (cmd_id == CMD_ENCODE)
			{
				if (!testmode)
				{
					model_t *in_model = ParseGLB (raw.data, raw.data_size);
					if (!in_model)
					{
						ERROR0 (ERR_INVALID_DATA, "Failed to parse model %s: %s\n",
							is_glb_input ? "GLB" : "DAE", arg);
						return ERR_INVALID_DATA;
					}
					if (is_hsf)
					{
						err = EncodeModelToHSF (in_model, dest);
						FreeModel (in_model);
						if (err > ERR_WARNING)
							ERROR0 (err, "Failed to encode HSF: %s\n", dest);
						continue;
					}
					if (is_hsd)
					{
						err = EncodeModelToHSD (in_model, dest);
						FreeModel (in_model);
						if (err > ERR_WARNING)
							ERROR0 (err, "Failed to encode HSD: %s\n", dest);
						else if (verbose >= 0)
							fprintf (stdlog, "%sENCODE HSD:%s -> %s\n", verbose > 0 ? "\n" : "",
								arg, dest);
						continue;
					}
					if (is_msh)
					{
						err = EncodeExciteMSH (in_model, dest);
						FreeModel (in_model);
						if (err > ERR_WARNING)
							ERROR0 (err, "Failed to encode MSH: %s\n", dest);
						else if (verbose >= 0)
							fprintf (stdlog, "%sENCODE MSH:%s -> %s\n", verbose > 0 ? "\n" : "",
								arg, dest);
						continue;
					}
					if (is_glg)
					{
						err = EncodeGLG (in_model, dest);
						FreeModel (in_model);
						if (err > ERR_WARNING)
							ERROR0 (err, "Failed to encode GLG: %s\n", dest);
						else if (verbose >= 0)
							fprintf (stdlog, "%sENCODE GLG:%s -> %s\n", verbose > 0 ? "\n" : "",
								arg, dest);
						continue;
					}
					if (is_mod)
					{
						err = EncodeExciteMOD (in_model, dest);
						FreeModel (in_model);
						if (err > ERR_WARNING)
							ERROR0 (err, "Failed to encode MOD: %s\n", dest);
						else if (verbose >= 0)
							fprintf (stdlog, "%sENCODE MOD:%s -> %s\n", verbose > 0 ? "\n" : "",
								arg, dest);
						continue;
					}
					if (is_nud)
					{
						err = EncodeModelToNUD (in_model, dest);
						FreeModel (in_model);
						if (err > ERR_WARNING)
							ERROR0 (err, "Failed to encode NUD: %s\n", dest);
						else if (verbose >= 0)
							fprintf (stdlog, "%sENCODE NUD:%s -> %s\n", verbose > 0 ? "\n" : "",
								arg, dest);
						continue;
					}
					if (is_bnfm)
					{
						err = EncodeModelToBNFM (in_model, dest);
						FreeModel (in_model);
						if (err > ERR_WARNING)
							ERROR0 (err, "Failed to encode BNFM: %s\n", dest);
						else if (verbose >= 0)
							fprintf (stdlog, "%sENCODE BNFM:%s -> %s\n", verbose > 0 ? "\n" : "",
								arg, dest);
						continue;
					}
					if (is_bfres && (!opt_parent || !*opt_parent))
					{
						uint8_t *created = NULL;
						size_t created_size = 0;
						if (CreateSwitchBFRES (in_model, &created, &created_size) && created)
						{
							SaveFILE (dest, 0, true, created, (uint)created_size, 0);
							FREE (created);
							FreeModel (in_model);
							if (verbose >= 0)
								fprintf (stdlog, "%sENCODE BFRES:%s -> %s\n",
									verbose > 0 ? "\n" : "", arg, dest);
							continue;
						}
					}

					char parent_path[PATH_MAX] = "";
					if (opt_parent && *opt_parent)
					{
						snprintf (parent_path, sizeof (parent_path), "%s", opt_parent);
					}
					else if (!access (dest, F_OK))
					{
						snprintf (parent_path, sizeof (parent_path), "%s", dest);
					}
					else
					{
						// Search for sibling parent BRRES or MDL0
						char cand[PATH_MAX];
						snprintf (cand, sizeof (cand), "%s", arg);
						char *dot = strrchr (cand, '.');
						if (dot)
						{
							snprintf (dot, sizeof (cand) - (dot - cand), ".brres");
							if (!access (cand, F_OK))
								snprintf (parent_path, sizeof (parent_path), "%s", cand);
							else
							{
								snprintf (dot, sizeof (cand) - (dot - cand), ".mdl0");
								if (!access (cand, F_OK))
									snprintf (parent_path, sizeof (parent_path), "%s", cand);
							}
						}
						// Also check if inside a directory like .../3DModels(NW4R)/
						if (!*parent_path)
						{
							char *slash = strrchr (arg, '/');
							if (slash)
							{
								char base[128];
								snprintf (base, sizeof (base), "%s", slash + 1);
								char *bdot = strrchr (base, '.');
								if (bdot)
									*bdot = 0;
								snprintf (cand, sizeof (cand), "%.*s/../../%s.brres",
									(int)(slash - arg), arg, base);
								if (!access (cand, F_OK))
									snprintf (parent_path, sizeof (parent_path), "%s", cand);
							}
						}
					}

					if (!*parent_path)
					{
						FreeModel (in_model);
						ERROR0 (ERR_INVALID_DATA,
							"No parent BRRES or MDL0 specified for model injection (use "
							"--parent=file)\n");
						return ERR_INVALID_DATA;
					}

					raw_data_t parent_raw;
					InitializeRawData (&parent_raw);
					err = LoadRawData (&parent_raw, false, parent_path, 0, false, 0);
					if (err > ERR_WARNING)
					{
						FreeModel (in_model);
						ERROR0 (err, "Failed to load parent file: %s\n", parent_path);
						return err;
					}

					uint8_t *out_buf = NULL;
					size_t out_len = 0;
					int ok = InjectDAEIntoModel (
						parent_raw.data, parent_raw.data_size, in_model, &out_buf, &out_len);
					if (!ok)
					{
						ERROR0 (ERR_INVALID_DATA,
							"Unsupported parent model format or injection failed for %s\n",
							parent_path);
						ResetRawData (&parent_raw);
						FreeModel (in_model);
						return ERR_INVALID_DATA;
					}

					if (ok && out_buf)
					{
						FILE *f = fopen (dest, "wb");
						if (f)
						{
							fwrite (out_buf, 1, out_len, f);
							fclose (f);
							if (verbose >= 0)
								fprintf (stdlog, "Injected %zu meshes from %s into %s -> %s\n",
									in_model->num_meshes, arg, parent_path, dest);
						}
						else
						{
							ERROR0 (ERR_CANT_CREATE, "Cannot create destination file: %s\n", dest);
							err = ERR_CANT_CREATE;
						}
						FREE (out_buf);
					}
					else
					{
						ERROR0 (ERR_INVALID_DATA,
							"Failed to inject model geometry into parent %s\n", parent_path);
						err = ERR_INVALID_DATA;
					}

					ResetRawData (&parent_raw);
					FreeModel (in_model);
					if (err > ERR_WARNING)
						return err;
				}
				continue;
			}
		}

		const bool is_hsf_in
			= is_ext (arg, ".hsf") || (raw.data_size >= 7 && !memcmp (raw.data, "HSFV037", 7));
		const bool is_bnfm_in
			= is_ext (arg, ".bnfm") || (raw.data_size >= 4 && !memcmp (raw.data, "BNFM", 4));
		const bool is_hsd_in = is_ext (arg, ".dat")
			|| (raw.data_size >= 0x40 && IsHSD (raw.data, (uint)raw.data_size));
		const bool is_msh_in
			= is_ext (arg, ".msh") || (raw.data_size >= 4 && (!memcmp (raw.data, "PMsh", 4) || !memcmp (raw.data, "hsMP", 4)));
		const bool is_mod_in = is_ext (arg, ".mod")
			|| (raw.data_size >= 4
				&& (!memcmp (raw.data, "NDL3", 4) || !memcmp (raw.data, "3LDN", 4)
					|| !memcmp (raw.data, "NDL2", 4) || !memcmp (raw.data, "2LDN", 4)));

		const bool is_nud_in = is_ext (arg, ".nud")
			|| (raw.data_size >= 4
				&& (!memcmp (raw.data, "NDP3", 4) || !memcmp (raw.data, "NDWU", 4)));

		const bool is_glg_in = is_ext (arg, ".glg") || is_ext (arg, ".rlg")
			|| (raw.data_size >= 4 && raw.data[0] == 0x80 && raw.data[1] == 0
				&& raw.data[2] == 0xb0 && (raw.data[3] == 0 || raw.data[3] == 1));

		if (is_model_dest && is_glg_in)
		{
			if (!testmode)
			{
				// 'arg' locates the sibling .glt/.rlt this model's textures live in.
				err = DecodeGLG2 (raw.data, (uint)raw.data_size, arg, dest);
				if (err > ERR_WARNING)
				{
					ERROR0 (err, "Failed to decode GLG: %s\n", arg);
					return err;
				}
			}
			continue;
		}

		if (is_model_dest && is_nud_in)
		{
			if (!testmode)
			{
				model_t *model = ParseNUD (raw.data, raw.data_size);
				if (model)
				{
					ExportModelToGLB (model, dest);
					FreeModel (model);
				}
				else
				{
					ERROR0 (ERR_INVALID_DATA, "Failed to decode NUD: %s\n", arg);
					return ERR_INVALID_DATA;
				}
			}
			continue;
		}

		if (is_model_dest && is_bnfm_in)
		{
			if (!testmode)
			{
				err = DecodeBNFM (raw.data, (uint)raw.data_size, dest);
				if (err > ERR_WARNING)
				{
					ERROR0 (err, "Failed to decode BNFM: %s\n", arg);
					return err;
				}
			}
			continue;
		}

		if (is_model_dest && is_hsf_in)
		{
			if (!testmode)
			{
				err = DecodeHSF (raw.data, (uint)raw.data_size, dest);
				if (err > ERR_WARNING)
				{
					ERROR0 (err, "Failed to decode HSF: %s\n", arg);
					return err;
				}
			}
			continue;
		}

		if (is_model_dest && is_hsd_in)
		{
			if (!testmode)
			{
				char dest_dir[PATH_MAX];
				snprintf (dest_dir, sizeof (dest_dir), "%s", dest);
				char *slash = strrchr (dest_dir, '/');
				if (slash)
					*slash = 0;
				else
					snprintf (dest_dir, sizeof (dest_dir), ".");
				char base[80];
				ccp arg_slash = strrchr (arg, '/');
				StringCopyS (base, sizeof (base), arg_slash ? arg_slash + 1 : arg);
				char *dot = strrchr (base, '.');
				if (dot)
					*dot = 0;

				ExportHSDTexturesFromData (raw.data, (uint)raw.data_size, dest_dir, base);
				int nm = ExportHSDModelFromData (raw.data, (uint)raw.data_size, dest);
				if (nm < 0)
				{
					ERROR0 (ERR_INVALID_DATA, "Failed to decode HSD: %s\n", arg);
					return ERR_INVALID_DATA;
				}
			}
			continue;
		}

		if (is_model_dest && is_msh_in)
		{
			if (!testmode)
			{
				err = DecodeExciteMSH (raw.data, (uint)raw.data_size, dest);
				if (err > ERR_WARNING)
				{
					ERROR0 (err, "Failed to decode MSH: %s\n", arg);
					return err;
				}
			}
			continue;
		}

		if (is_model_dest && is_mod_in)
		{
			if (!testmode)
			{
				err = DecodeExciteMOD (raw.data, (uint)raw.data_size, dest);
				if (err > ERR_WARNING)
				{
					ERROR0 (err, "Failed to decode MOD: %s\n", arg);
					return err;
				}
			}
			continue;
		}

		// BMD0/CGFX(BCH)/FRES are foreign 3D model containers, not Wiimm's
		// own MDL/MDL0 format -- ScanRawDataMDL() rejects them outright, so
		// they must be dispatched to their own parsers *before* that call
		const bool is_bmd
			= is_ext (arg, ".bmd") || (raw.data_size >= 4 && !memcmp (raw.data, "BMD0", 4));
		// SSBH belongs in this list too: the ParseNUMSHB() call further down
		// was unreachable without it, so a .numshb fell through to
		// ScanRawDataMDL() and came back as "No MDL file".
		if (is_model_dest
			&& (is_bmd
				|| (raw.data_size >= 4
					&& (!memcmp (raw.data, "CGFX", 4) || !memcmp (raw.data, "FRES", 4)
						|| !memcmp (raw.data, "BCH\0", 4) || !memcmp (raw.data, "SSBH", 4)
						|| !memcmp (raw.data, "HBSS", 4)))))
		{
			if (!testmode)
			{
				if (raw.data_size >= 4 && !memcmp (raw.data, "BCH\0", 4))
					ExportBCHTexturesFromData (raw.data, (uint)raw.data_size, dest);
				else if (raw.data_size >= 4 && !memcmp (raw.data, "CGFX", 4))
					ExportBCRESTexturesFromData (raw.data, raw.data_size, dest);
				else if (is_bmd)
					ExportEarlyDSBMDTextures (raw.data, raw.data_size, dest);

				model_t *model = is_bmd				? ParseNSBMD (raw.data, raw.data_size)
					: !memcmp (raw.data, "CGFX", 4) ? ParseBCRES (raw.data, raw.data_size)
					: !memcmp (raw.data, "BCH\0", 4)
					? (model_t *)ParseBCH (raw.data, (uint)raw.data_size)
					: ParseBFRES (raw.data, raw.data_size);
				if (!model && raw.data_size >= 4 && !memcmp (raw.data, "FRES", 4))
					model = ParseBFRESSwitch (raw.data, raw.data_size);
				if (!model && raw.data_size >= 4 && (!memcmp (raw.data, "NDP3", 4) || !memcmp (raw.data, "NDWU", 4)))
					model = ParseNUD (raw.data, raw.data_size);
				if (!model && raw.data_size >= 4 && (!memcmp (raw.data, "SSBH", 4) || !memcmp (raw.data, "HBSS", 4)))
				{
					// A mesh's bones live in a sibling .nusktb; without it the
					// model still exports, just unskinned.
					u8 *skel = 0;
					size_t skel_size = 0;
					char dir[PATH_MAX];
					snprintf (dir, sizeof (dir), "%s", arg);
					char *slash = strrchr (dir, '/');
					if (slash)
					{
						*slash = 0;
						DIR *dp = opendir (dir);
						if (dp)
						{
							for (const struct dirent *de; (de = readdir (dp));)
							{
								const size_t n = strlen (de->d_name);
								if (n < 8 || strcasecmp (de->d_name + n - 7, ".nusktb"))
									continue;
								char path[PATH_MAX];
								snprintf (path, sizeof (path), "%s/%s", dir, de->d_name);
								if (LoadFileAlloc (path, 0, 0, &skel, &skel_size, 0, 0, 0, false))
								{
									skel = 0;
									skel_size = 0;
									continue;
								}
								// The archive names files by hash, so a
								// ".nusktb" can hold something else entirely
								// -- a MATL turns up under that name. Keep
								// looking until one really is a skeleton.
								if (skel_size > 0x14 && !memcmp (skel, "HBSS", 4)
									&& (!memcmp (skel + 0x10, "LEKS", 4)
										|| !memcmp (skel + 0x10, "SKEL", 4)))
									break;
								FREE (skel);
								skel = 0;
								skel_size = 0;
							}
							closedir (dp);
						}
					}
					model = ParseNUMSHBSkinned (
						raw.data, raw.data_size, skel, skel_size);
					FREE (skel);
				}
				if (model)
				{
					if (is_dae)
						ExportModelToGLB (model, dest);
					else
						ExportModelToGLB (model, dest);
					FreeModel (model);
				}
				else
				{
					ERROR0 (ERR_INVALID_DATA, "Failed to parse 3D model: %s\n", arg);
					return ERR_INVALID_DATA;
				}
			}
			continue;
		}

		if (is_model_dest && !testmode)
		{
			enumError archive_err;
			if (export_mdl0_from_archive (&raw, dest, &archive_err))
			{
				if (archive_err > ERR_WARNING)
					return archive_err;
				continue;
			}
		}

		mdl_t mdl;
		err = ScanRawDataMDL (&mdl, true, &raw, global_check_mode);
		if (err > ERR_WARNING)
			return err;

		if (!testmode)
		{
			if (is_model_dest)
			{
				model_t *model = ParseMDL0 (raw.data, raw.data_size);
				if (model)
				{
					if (is_dae)
						ExportModelToGLB (model, dest);
					else
						ExportModelToGLB (model, dest);
					FreeModel (model);
				}
				else
				{
					err = ERR_ERROR;
				}
			}
			else
			{
				err = dest_ff == FF_MDL ? SaveRawMDL (&mdl, dest, opt_preserve)
										: SaveTextMDL (&mdl, dest, opt_preserve);
			}
			if (err > ERR_WARNING)
				return err;
		}
		ResetMDL (&mdl);
	}

	ResetStringField (&plist);
	ResetRawData (&raw);
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command strings			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError iter_strings (mdl_t *mdl, // MDL data structure
	void *param // a user defined parameter
)
{
	DASSERT (mdl);
	putchar ('\n');
	PrintStringsMDL (stdout, 0, mdl);
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_strings ()
{
	stdlog = stderr;

	raw_data_t raw;
	InitializeRawData (&raw);

	enumError cmd_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];
		enumError err = LoadRawData (&raw, false, arg, 0, opt_ignore > 0, 0);
		if (err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore)
			continue;
		if (err > ERR_WARNING)
			return err;

		IterateRawDataMDL (&raw, global_check_mode, iter_strings, 0);
	}

	putchar ('\n');
	ResetStringField (&plist);
	ResetRawData (&raw);
	return cmd_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command geometry		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError iter_geometry (mdl_t *mdl, // MDL data structure
	void *param // a user defined parameter
)
{
	DASSERT (mdl);

	if (verbose >= 0 || testmode)
	{
		printf ("%sGeometry of %s:%s\n", verbose > 0 ? "\n" : "", GetNameFF (mdl->fform, 0),
			mdl->fname);
		fflush (stdout);
	}

	// ???

	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_geometry ()
{
	stdlog = stderr;

	raw_data_t raw;
	InitializeRawData (&raw);

	enumError cmd_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];
		enumError err = LoadRawData (&raw, false, arg, 0, opt_ignore > 0, 0);
		if (err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore)
			continue;
		if (err > ERR_WARNING)
			return err;

		IterateRawDataMDL (&raw, global_check_mode, iter_geometry, 0);
	}

	ResetStringField (&plist);
	ResetRawData (&raw);
	return cmd_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command _TEST			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError iter_xtest (mdl_t *mdl, // MDL data structure
	void *param // a user defined parameter
)
{
	DASSERT (mdl);

#if 1 // dump MDL section order -------------------------------------------

	// old_mario_gc_*.szs   9,0,1,6,7,8,     2,3,4,5
	// all others:         11,0,1,    8,9,10,2,3,4,5
	static char order[] = { 11, 0, 1, 6, 7, 8, 9, 10, 2, 3, 4, 5, -1 };
	static char ref_v8[20], ref_v11[20] = { -1 };
	if (ref_v11[0] == -1)
	{
		memset (ref_v11, 0, sizeof (ref_v11));
		uint i;
		for (i = 0; order[i] >= 0; i++)
			ref_v11[(int)order[i]] = i + 2;
		HexDump16 (0, 0, 0, ref_v11, sizeof (ref_v11));

		memcpy (ref_v8, ref_v11, sizeof (ref_v8));
		ref_v8[9] = 1;
	}
	ccp ref = mdl->version == 8 ? ref_v8 : ref_v11;

	printf ("#ORDER: ");
	char sep = ' ';
	int sect = -1, last_ref = -1, fail = 0;

	SortMIL (&mdl->elem, true);

	const MemItem_t *mi = GetMemListElem (&mdl->elem, 0, 0);
	const MemItem_t *mi_end = mi + mdl->elem.used;
	for (; mi < mi_end; mi++)
	{
		if (sect != mi->idx1)
		{
			sect = mi->idx1;
			printf ("%c%d", sep, sect);
			sep = ',';

			if (ref[sect] <= last_ref)
				fail++;
			last_ref = ref[sect];
		}
	}
	printf (" %s: %s\n", fail ? "#FAIL! " : "", mdl->fname);

#endif //------------------------------------------------------------------
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

static enumError cmd_xtest ()
{
	stdlog = stderr;

	raw_data_t raw;
	InitializeRawData (&raw);

	enumError cmd_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];
		enumError err = LoadRawData (&raw, false, arg, 0, opt_ignore > 0, 0);
		if (err == ERR_NOT_EXISTS || err > ERR_WARNING && opt_ignore)
			continue;
		if (err > ERR_WARNING)
			return err;

		IterateRawDataMDL (&raw, global_check_mode, iter_xtest, 0);
	}

	putchar ('\n');
	ResetStringField (&plist);
	ResetRawData (&raw);
	return cmd_err;
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

		RegisterOptionByName (&InfoUI_wmdlt, opt_stat, 1, is_env);

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
				err += ScanOptConst (optarg);
				break;
			case GO_MDL:
				err += ScanOptMdl (optarg);
				break;
			case GO_SCALE:
				err += ScanOptScale (optarg);
				break;
			case GO_SHIFT:
				err += ScanOptShift (optarg);
				break;
			case GO_XSS:
				err += ScanOptXSS (0, optarg);
				break;
			case GO_YSS:
				err += ScanOptXSS (1, optarg);
				break;
			case GO_ZSS:
				err += ScanOptXSS (2, optarg);
				break;
			case GO_ROT:
				err += ScanOptRotate (optarg);
				break;
			case GO_XROT:
				err += ScanOptXRotate (0, optarg);
				break;
			case GO_YROT:
				err += ScanOptXRotate (1, optarg);
				break;
			case GO_ZROT:
				err += ScanOptXRotate (2, optarg);
				break;
			case GO_TRANSLATE:
				err += ScanOptTranslate (optarg);
				break;
			case GO_NULL:
				force_transform |= 1;
				break;
			case GO_NEXT:
				err += NextTransformation (false);
				break;
			case GO_ASCALE:
				err += ScanOptAScale (optarg);
				break;
			case GO_AROT:
				err += ScanOptARotate (optarg);
				break;
			case GO_TFORM_SCRIPT:
				err += ScanOptTformScript (optarg);
				break;

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
			case GO_TINY:
				err += ScanOptTiny (optarg);
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
			case GO_PARENT:
				opt_parent = optarg;
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

			case GO_ROUND:
				opt_round = true;
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
			case GO_NO_PARAM:
				print_param = false;
				break;
			case GO_NO_ECHO:
				opt_no_echo = true;
				break;
			case GO_NO_CHECK:
				opt_no_check = true;
				break;
			case GO_SECTIONS:
				print_sections++;
				break;

				// no default case defined
				//	=> compiler checks the existence of all enum values
		}
	}

#ifdef DEBUG
	DumpUsedOptions (&InfoUI_wmdlt, TRACE_FILE, 11);
#endif
	CloseTransformation ();
	NormalizeOptions (verbose > 3 && !is_env);
	SetupMDL ();

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
		enumError err = VerifySpecificOptions (&InfoUI_wmdlt, cmd_ct);
		if (err)
			hint_exit (err);
	}
	WarnDepractedOptions (&InfoUI_wmdlt);

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
			PrintHelpColor (&InfoUI_wmdlt);
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
		case CMD_EXPORT:
			err = cmd_export ();
			break;

		case CMD_SYMBOLS:
			err = DumpSymbols (SetupVarsMDL ());
			break;
		case CMD_FUNCTIONS:
			SetupVarsMDL ();
			err = ListParserFunctions ();
			break;
		case CMD_CALCULATE:
			err = ParserCalc (SetupVarsMDL ());
			break;
		case CMD_MATRIX:
			err = cmd_matrix ();
			break;
		case CMD_FLOAT:
			err = cmd_float ();
			break;

		case CMD_CAT:
			err = cmd_cat ();
			break;
		case CMD_DECODE:
			err = cmd_convert (cmd_ct->id, "DECODE", "\1P/\1N.txt");
			break;
		case CMD_ENCODE:
			err = cmd_convert (cmd_ct->id, "ENCODE", "\1P/\1N\1?T");
			break;
		case CMD_STRINGS:
			err = cmd_strings ();
			break;
		case CMD_GEOMETRY:
			err = cmd_geometry ();
			break;
		case CMD_XTEST:
			err = cmd_xtest ();
			break;

			// no default case defined
			//	=> compiler checks the existence of all enum values

		case CMD__NONE:
		case CMD__N:
			help_exit (false);
	}

	return PrintErrorStat (err, verbose, cmd_ct->name1);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			   main()			///////////////
///////////////////////////////////////////////////////////////////////////////

#if SZS_WRAPPER
int main_wmdlt (int argc, char **argv)
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

	tool_name = "wmdlt";
	print_title_func = print_title;
	SetupLib (argc, argv, WMDLT_SHORT, VERSION, TITLE);

	//----- process arguments

	if (argc < 2)
	{
		printf ("\n%s\n%s\nVisit %s%s for more info.\n\n", text_logo, TITLE, URI_HOME, WMDLT_SHORT);
		hint_exit (ERR_OK);
	}

	enumError err = CheckEnvOptions2 ("WMDLT_OPT", CheckOptions);
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
