//
///////////////////////////////////////////////////////////////////////////////
///////////////		    command SPRITES			///////////////
///////////////////////////////////////////////////////////////////////////////

// Loads a whole file, returning NULL on any failure.
static u8 *sprite_load (ccp path, uint *size)
{
	u8 *data = 0;
	size_t fsize = 0;
	if (LoadFileAlloc (path, 0, 0, &data, &fsize, 0, 2, 0, false) || fsize > UINT_MAX)
	{
		FREE (data);
		return 0;
	}
	*size = (uint)fsize;
	return data;
}

static u8 *sprite_load_companion (ccp dir, ccp base, ccp ext, uint *size)
{
	char path[PATH_MAX];
	char upext[16];
	for (int i = 0; ext[i]; i++)
		upext[i] = toupper ((u8)ext[i]);
	upext[strlen (ext)] = 0;

	const char *suffixes[] = { "", ".P", ".p", ".bin" };

	// Generate up to 4 candidate base stems
	char stems[4][PATH_MAX];
	int n_stems = 0;

	// 1. Exact base
	snprintf (stems[n_stems++], sizeof (stems[0]), "%s", base);

	// 2. Strip hyphen/suffix (e.g. "CompanyBDojoT_D07-4" -> "CompanyBDojoT_D07")
	char stem2[PATH_MAX];
	snprintf (stem2, sizeof (stem2), "%s", base);
	char *dash = strrchr (stem2, '-');
	if (dash && dash > stem2)
	{
		*dash = 0;
		snprintf (stems[n_stems++], sizeof (stems[0]), "%s", stem2);
	}

	// 3. Strip trailing digits (e.g. "Touch1" -> "Touch", "GPAsDemo01B02" -> "GPAsDemo01B")
	char stem3[PATH_MAX];
	snprintf (stem3, sizeof (stem3), "%s", n_stems > 1 ? stems[1] : stems[0]);
	int l3 = (int)strlen (stem3);
	while (l3 > 2 && isdigit ((u8)stem3[l3 - 1]))
		stem3[--l3] = 0;
	if (l3 > 2 && strcmp (stem3, stems[0]))
	{
		snprintf (stems[n_stems++], sizeof (stems[0]), "%s", stem3);
	}

	// 4. Strip trailing bank/layer letter (e.g. "GPAsDemo01B" -> "GPAsDemo01", "GPAsDemo")
	if (n_stems > 0)
	{
		char stem4[PATH_MAX];
		snprintf (stem4, sizeof (stem4), "%s", stems[n_stems - 1]);
		int l4 = (int)strlen (stem4);
		if (l4 > 2
			&& (stem4[l4 - 1] == 'T' || stem4[l4 - 1] == 'B' || stem4[l4 - 1] == 'O'
				|| stem4[l4 - 1] == 'L' || stem4[l4 - 1] == 'S'))
			stem4[--l4] = 0;
		while (l4 > 2 && isdigit ((u8)stem4[l4 - 1]))
			stem4[--l4] = 0;
		if (l4 > 2 && strcmp (stem4, stems[0]) && (n_stems < 2 || strcmp (stem4, stems[1]))
			&& (n_stems < 3 || strcmp (stem4, stems[2])))
		{
			snprintf (stems[n_stems++], sizeof (stems[0]), "%s", stem4);
		}
	}

	for (int i = 0; i < n_stems; i++)
	{
		for (int c = 0; c < 2; c++)
		{
			ccp e = c == 0 ? upext : ext;
			for (uint s = 0; s < sizeof (suffixes) / sizeof (*suffixes); s++)
			{
				snprintf (path, sizeof (path), "%s%s.%s%s", dir, stems[i], e, suffixes[s]);
				if (access (path, F_OK) != 0)
					continue;
				u8 *raw = 0;
				size_t raw_sz = 0;
				if (!LoadFileAlloc (path, 0, 0, &raw, &raw_sz, 0, 2, 0, false) && raw_sz > 0
					&& raw_sz <= UINT_MAX)
				{
					if ((raw[0] == 0x10 || raw[0] == 0x11) && raw_sz >= 4)
					{
						u8 *dec = 0;
						uint dec_sz = 0;
						if (DecodeLZ10LZ11 (&dec, &dec_sz, raw, (uint)raw_sz) == ERR_OK && dec)
						{
							FREE (raw);
							*size = dec_sz;
							return dec;
						}
					}
					*size = (uint)raw_sz;
					return raw;
				}
				FREE (raw);
			}
		}
	}

	return 0;
}

// Writes a tightly packed RGBA8 buffer as a PNG through the image layer.
static enumError sprite_save_png (ccp path, u8 *rgba, uint w, uint h)
{
	Image_t img;
	InitializeIMG (&img);
	// AssignIMG's decoders own their pixels; hand over a copy so the caller
	// keeps control of its own buffer.
	const uint xw = EXPAND8 (w), xh = EXPAND8 (h);
	u8 *data = CALLOC (1, (size_t)xw * xh * 4);
	if (!data)
		return ERR_CANT_CREATE;
	for (uint y = 0; y < h; y++)
		memcpy (data + (size_t)y * xw * 4, rgba + (size_t)y * w * 4, (size_t)w * 4);
	img.data = data;
	img.data_alloced = true;
	img.data_size = xw * xh * 4;
	img.width = w;
	img.xwidth = xw;
	img.height = h;
	img.xheight = xh;
	img.iform = img.info_iform = IMG_X_RGB;
	img.info_fform = FF_UNKNOWN;
	img.info_n_image = 1;
	img.alpha_status = 0;
	img.endian = &le_func;
	img.path = path;
	const enumError err = SaveIMG (&img, FF_PNG, 0, 0, path, true);
	ResetIMG (&img);
	return err;
}

// Given one file from a Nitro sprite set, find its siblings by base name and
// render whatever the available combination supports.  This is the
// "autoguess" front end: point it at a folder (or any member) and it works
// out which files belong together and what the right export is.
static enumError sprites_from_base (ccp dir, ccp base)
{
	char path[PATH_MAX];
	uint ncgr_size = 0, nclr_size = 0, ncer_size = 0, nanr_size = 0;
	u8 *ncgr_data = sprite_load_companion (dir, base, "ncgr", &ncgr_size);
	u8 *nclr_data = sprite_load_companion (dir, base, "nclr", &nclr_size);
	u8 *ncer_data = sprite_load_companion (dir, base, "ncer", &ncer_size);
	u8 *nanr_data = sprite_load_companion (dir, base, "nanr", &nanr_size);

	enumError err = ERR_OK;
	if (!ncgr_data || !nclr_data)
	{
		// Not a usable set; a lone NCGR still decodes as a tile sheet through
		// the normal wimgt path, so this is not an error.
		err = ERR_NOTHING_TO_DO;
		goto done;
	}

	nitro_ncgr_t ncgr;
	nitro_nclr_t nclr;
	memset (&ncgr, 0, sizeof (ncgr));
	memset (&nclr, 0, sizeof (nclr));
	if (ScanNitroNCGR (&ncgr, ncgr_data, ncgr_size))
	{
		err = ERROR0 (ERR_INVALID_DATA, "Invalid NCGR: %s%s.ncgr\n", dir, base);
		goto done;
	}
	if (ScanNitroNCLR (&nclr, nclr_data, nclr_size))
	{
		err = ERROR0 (ERR_INVALID_DATA, "Invalid NCLR: %s%s.nclr\n", dir, base);
		goto done;
	}

	if (!ncer_data)
	{
		// NCGR + NCLR only: the paletted tile sheet is what wimgt already
		// produces, so there is nothing extra to composite here.
		if (verbose >= 0)
			fprintf (stdlog,
				"SPRITES %s: NCGR+NCLR only,"
				" use 'wimgt DECODE %s%s.ncgr' for the tile sheet\n",
				base, dir, base);
		ResetNitroNCLR (&nclr);
		err = ERR_OK;
		goto done;
	}

	nintendo_ncer_t ncer;
	memset (&ncer, 0, sizeof (ncer));
	if (ScanNCER (&ncer, ncer_data, ncer_size))
	{
		ResetNitroNCLR (&nclr);
		err = ERROR0 (ERR_INVALID_DATA, "Invalid NCER: %s%s.ncer\n", dir, base);
		goto done;
	}

	char destdir[PATH_MAX];
	if (opt_dest && *opt_dest)
		snprintf (destdir, sizeof (destdir), "%s", opt_dest);
	else
		snprintf (destdir, sizeof (destdir), "%s%s.d", dir, base);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%sSPRITES %s: %u cell%s, %u tiles @%ubpp, %u colors -> %s/\n",
			testmode ? "WOULD " : "", base, ncer.n_cells, ncer.n_cells == 1 ? "" : "s",
			ncgr.n_tiles, ncgr.bpp, nclr.n_entries, destdir);

	uint written = 0;
	for (uint i = 0; !err && i < ncer.n_cells; i++)
	{
		u8 *rgba = 0;
		uint w = 0, h = 0;
		int ox = 0, oy = 0;
		if (RenderNCERCell (&rgba, &w, &h, &ox, &oy, &ncer, i, &ncgr, &nclr))
			continue; // empty or unrenderable cell
		if (!testmode)
		{
			snprintf (path, sizeof (path), "%s/cell_%03u.png", destdir, i);
			err = sprite_save_png (path, rgba, w, h);
		}
		FREE (rgba);
		written++;
	}

	// NANR: emit one image per animation frame by reusing the cell renders.
	if (!err && nanr_data)
	{
		nintendo_nanr_t nanr;
		memset (&nanr, 0, sizeof (nanr));
		if (!ScanNANR (&nanr, nanr_data, nanr_size))
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "SPRITES %s: %u animation%s\n", base, nanr.n_animations,
					nanr.n_animations == 1 ? "" : "s");
			for (uint a = 0; !err && a < nanr.n_animations; a++)
			{
				uint n_frames = 0;
				const u8 *frames = 0;
				if (GetNANRAnimation (&nanr, a, &n_frames, &frames))
					continue;
				for (uint f = 0; !err && f < n_frames; f++)
				{
					// Each frame record starts with the cell index it shows.
					const uint cell = (uint)frames[f * 8] | (uint)frames[f * 8 + 1] << 8;
					if (cell >= ncer.n_cells)
						continue;
					u8 *rgba = 0;
					uint w = 0, h = 0;
					int ox = 0, oy = 0;
					if (RenderNCERCell (&rgba, &w, &h, &ox, &oy, &ncer, cell, &ncgr, &nclr))
						continue;
					if (!testmode)
					{
						snprintf (path, sizeof (path), "%s/anim%02u_frame%03u.png", destdir, a, f);
						err = sprite_save_png (path, rgba, w, h);
					}
					FREE (rgba);
					written++;
				}
			}
		}
	}

	if (verbose >= 0)
		fprintf (
			stdlog, "SPRITES %s: %u image%s written\n", base, written, written == 1 ? "" : "s");

	ResetNitroNCLR (&nclr);

done:
	FREE (ncgr_data);
	FREE (nclr_data);
	FREE (ncer_data);
	FREE (nanr_data);
	return err;
}

static enumError nscr_from_base (ccp dir, ccp base)
{
	uint nscr_size = 0, ncgr_size = 0, nclr_size = 0;
	u8 *nscr_data = sprite_load_companion (dir, base, "nscr", &nscr_size);
	if (!nscr_data)
		return ERR_NOTHING_TO_DO;

	u8 *ncgr_data = sprite_load_companion (dir, base, "ncgr", &ncgr_size);
	u8 *nclr_data = sprite_load_companion (dir, base, "nclr", &nclr_size);

	if (!ncgr_data || !nclr_data)
	{
		FREE (nscr_data);
		FREE (ncgr_data);
		FREE (nclr_data);
		return ERR_NOTHING_TO_DO;
	}

	nitro_nscr_t nscr;
	nitro_ncgr_t ncgr;
	nitro_nclr_t nclr;
	memset (&nscr, 0, sizeof (nscr));
	memset (&ncgr, 0, sizeof (ncgr));
	memset (&nclr, 0, sizeof (nclr));
	if (ScanNitroNSCR (&nscr, nscr_data, nscr_size) || ScanNitroNCGR (&ncgr, ncgr_data, ncgr_size)
		|| ScanNitroNCLR (&nclr, nclr_data, nclr_size))
	{
		ResetNitroNCLR (&nclr);
		FREE (nscr_data);
		FREE (ncgr_data);
		FREE (nclr_data);
		return ERR_NOTHING_TO_DO;
	}

	u8 *rgba = 0;
	uint w = 0, h = 0;
	enumError err = RenderNSCR (&rgba, &w, &h, &nscr, &ncgr, &nclr);
	ResetNitroNCLR (&nclr);
	FREE (nscr_data);
	FREE (ncgr_data);
	FREE (nclr_data);
	if (err || !rgba)
	{
		FREE (rgba);
		return err ? err : ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest && *opt_dest)
		SubstDest (dest, sizeof (dest), base, opt_dest, 0, ".png", false);
	else
		snprintf (dest, sizeof (dest), "%s%s.png", dir, base);

	if (!opt_overwrite && !access (dest, F_OK))
	{
		FREE (rgba);
		return ERR_OK;
	}

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE NSCR:%s%s -> PNG:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", dir, base, dest);

	if (!testmode)
		err = sprite_save_png (dest, rgba, w, h);

	FREE (rgba);
	return err;
}

static enumError decode_image_if_possible (ccp arg)
{
	if (export_count <= 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;

	// Fast signature/extension probe before loading into memory
	u8 head[256];
	FILE *probe = fopen (arg, "rb");
	if (!probe)
		return ERR_NOT_EXISTS;
	struct stat st_probe;
	const bool have_stat = !fstat (fileno (probe), &st_probe);
	const size_t n_head = fread (head, 1, sizeof (head), probe);
	fclose (probe);
	if (n_head < 4)
		return ERR_NOTHING_TO_DO;

	// Don't decode already decoded PNGs, XMLs, YAMLs, GLBs, etc.
	if (!memcmp (head, "\x89PNG", 4) || is_ext (arg, ".png") || is_ext (arg, ".xml")
		|| is_ext (arg, ".yaml") || is_ext (arg, ".yml") || is_ext (arg, ".tflyt")
		|| is_ext (arg, ".dae") || is_ext (arg, ".glb"))
		return ERR_NOTHING_TO_DO;

	bool is_image = false;
	const file_format_t fform = GetByMagicFF (head, (uint)n_head, 0);
	const nfmt_info_t nfmt = DetectNintendoFormat (head, (uint)n_head, arg);

	// NFTR/BRFNT decoding emits sheet PNGs, but no lossless sheet injector
	// exists: the standalone encoder constructs a new font and cannot retain
	// the original character map, widths, or multiple TGLP sheets.  Keep game
	// fonts opaque in recursive game-image staging rather than accepting edits
	// that CREATE would silently discard.
	if (nfmt.type == NFMT_NFTR || nfmt.type == NFMT_BRFNT || is_ext (arg, ".nftr")
		|| is_ext (arg, ".zftr") || is_ext (arg, ".brfnt") || is_ext (arg, ".bffnt"))
		return ERR_NOTHING_TO_DO;

	if (!memcmp (head, "RGCN", 4) || !memcmp (head, "NCGR", 4) || is_ext (arg, ".ncgr"))
		is_image = true;
	else if (!memcmp (head, "RLCN", 4) || !memcmp (head, "NCLR", 4) || is_ext (arg, ".nclr"))
		is_image = true;
	else if (!memcmp (head, "RECN", 4) || !memcmp (head, "NCER", 4) || is_ext (arg, ".ncer"))
		is_image = true;
	else if (!memcmp (head, "RCSN", 4) || !memcmp (head, "NSCR", 4) || is_ext (arg, ".nscr"))
		is_image = true;
	else if (!memcmp (head, "BNTX", 4) || is_ext (arg, ".bntx"))
		is_image = true;
	// GSH shares the Gfx2 container with GTX but has no pixels. Let the
	// normal archive/analyse path handle it instead of reporting a generic
	// failed-image-decode error.
	else if ((!memcmp (head, "Gfx2", 4) && !is_ext (arg, ".gsh")) || is_ext (arg, ".gtx"))
		is_image = true;
	else if (!memcmp (head, "CTPK", 4) || is_ext (arg, ".ctpk"))
		is_image = true;
	else if (!memcmp (head, "AJPG", 4) || is_ext (arg, ".ajpg") || is_ext (arg, ".odh"))
		is_image = true;
	else if (!memcmp (head, "TXTR", 4) || is_ext (arg, ".dsb"))
		is_image = true;
	// Retro Studios TXTR: Tropical Freeze is an RFRM form with the TXTR
	// form id at 0x14 (RFRM alone is generic to many Tropical formats, so
	// the id must match); old Retro (Metroid Prime/DKCR) has no magic, so
	// tiny textures are content-sniffed while the rest arrive via the
	// .txtr extension (the 256-byte probe rarely holds a full base level).
	else if ((!memcmp (head, "RFRM", 4) && n_head >= 0x18 && !memcmp (head + 0x14, "TXTR", 4))
		|| IsRetroTXTR (head, (uint)n_head) || is_ext (arg, ".txtr"))
		is_image = true;
	else if (fform == FF_GVR || is_ext (arg, ".gvr"))
	{
		// GVR covers more Sega texture encodings than LoadIMG currently
		// implements.  Leave an unknown variant opaque: it remains perfectly
		// round-trippable as its raw member, whereas sending it to LoadIMG()
		// produces one hard error for every non-editable asset in Sonic games.
		const uint gvr_format = n_head > 0x1b ? head[0x1b] : 0xff;
		is_image
			= n_head >= 0x20 && !(head[0x1a] & 0x0b) && (gvr_format <= 6 || gvr_format == 0x0e);
	}
	else if (nfmt.type == NFMT_BFLIM || nfmt.type == NFMT_BCLIM || is_ext (arg, ".bflim")
		|| is_ext (arg, ".bclim"))
		is_image = true;
	else if (fform == FF_TPL || is_ext (arg, ".tpl"))
		is_image = true;
	else if (fform == FF_TEX || is_ext (arg, ".tex0"))
		is_image = true;
	else if (fform == FF_NTTF || nfmt.type == NFMT_NTTF || is_ext (arg, ".nttf")
		|| is_ext (arg, ".bnttf") || strstr (arg, "/nttf/")
		|| (have_stat && IsNTTF (head, (uint)st_probe.st_size)))
		is_image = true;

	if (!is_image)
		return ERR_NOTHING_TO_DO;

	// If it's an NCER, sprites_from_base composites full cells
	if (!memcmp (head, "RECN", 4) || !memcmp (head, "NCER", 4) || is_ext (arg, ".ncer"))
	{
		char dir[PATH_MAX], base[PATH_MAX];
		ccp slash = strrchr (arg, '/');
		if (slash)
		{
			snprintf (dir, sizeof (dir), "%.*s/", (int)(slash - arg), arg);
			snprintf (base, sizeof (base), "%s", slash + 1);
		}
		else
		{
			snprintf (dir, sizeof (dir), "./");
			snprintf (base, sizeof (base), "%s", arg);
		}
		char *dot = strrchr (base, '.');
		if (dot)
			*dot = 0;
		return sprites_from_base (dir, base);
	}

	// If it's an NSCR, render background screen
	if (!memcmp (head, "RCSN", 4) || !memcmp (head, "NSCR", 4) || is_ext (arg, ".nscr"))
	{
		char dir[PATH_MAX], base[PATH_MAX];
		ccp slash = strrchr (arg, '/');
		if (slash)
		{
			snprintf (dir, sizeof (dir), "%.*s/", (int)(slash - arg), arg);
			snprintf (base, sizeof (base), "%s", slash + 1);
		}
		else
		{
			snprintf (dir, sizeof (dir), "./");
			snprintf (base, sizeof (base), "%s", arg);
		}
		char *dot = strrchr (base, '.');
		if (dot)
			*dot = 0;
		return nscr_from_base (dir, base);
	}

	Image_t img;
	enumError err = LoadIMG (&img, true, arg, 0, false, true, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (!img.data)
	{
		ResetIMG (&img);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".png", false);
	else
		snprintf (dest, sizeof (dest), "%s.png", arg);

	if (!opt_overwrite && !access (dest, F_OK))
	{
		ResetIMG (&img);
		return ERR_OK;
	}

	const uint record_images = img.info_n_image > 1 ? img.info_n_image : 1;
	if (record_images <= 1)
	{
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE %s:%s -> PNG:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", GetNameFF (img.info_fform, 0), arg, dest);
		if (!testmode)
		{
			Transform2XIMG (&img);
			err = SavePNG (&img, false, 0, dest, 0, 0, opt_overwrite > 0, 0);
			struct stat st_src;
			if (!stat (arg, &st_src))
			{
				struct utimbuf ut;
				ut.actime = st_src.st_atime;
				ut.modtime = st_src.st_mtime;
				utime (dest, &ut);
			}
		}
		ResetIMG (&img);
		return err ? err : ERR_OK;
	}

	ResetIMG (&img);
	char dir_dest[PATH_MAX];
	snprintf (dir_dest, sizeof (dir_dest), "%s.d", arg);
	for (uint image_index = 0; image_index < record_images; image_index++)
	{
		char sub_dest[PATH_MAX];
		snprintf (sub_dest, sizeof (sub_dest), "%s/image_%03u.png", dir_dest, image_index);
		// Same "already decoded" rule as the single-image "<file>.png" check
		// above, per image: that check never matches a multi-image output
		// directory, so XX's second whole-tree pass (the model/texture export
		// sweep) re-decoded e.g. Tetris Party Deluxe's system/save.tpl and
		// failed with "File already exists" on every image.
		if (!opt_overwrite && !access (sub_dest, F_OK))
			continue;
		const enumError load_err = LoadIMG (&img, true, arg, image_index, false, true, false);
		if (load_err)
			continue;
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE %s:%s[%u] -> PNG:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", GetNameFF (img.info_fform, 0), arg, image_index,
				sub_dest);
		if (!testmode)
		{
			Transform2XIMG (&img);
			SavePNG (&img, false, 0, sub_dest, 0, 0, opt_overwrite > 0, 0);
			struct stat st_src;
			if (!stat (arg, &st_src))
			{
				struct utimbuf ut;
				ut.actime = st_src.st_atime;
				ut.modtime = st_src.st_mtime;
				utime (sub_dest, &ut);
			}
		}
		ResetIMG (&img);
	}
	return ERR_OK;
}

static enumError cmd_sprites (void)
{
	if (!n_param)
		return ERROR0 (ERR_SYNTAX, "Command SPRITES needs a directory or file\n");

	opt_mkdir = true;
	enumError max_err = ERR_OK;

	for (ParamList_t *param = first_param; param; param = param->next)
	{
		ccp arg = param->arg;
		struct stat st;
		if (stat (arg, &st))
		{
			if (max_err < ERR_CANT_OPEN)
				max_err = ERROR0 (ERR_CANT_OPEN, "Can't access: %s\n", arg);
			continue;
		}

		if (S_ISDIR (st.st_mode))
		{
			// Collect the distinct base names of every Nitro graphic file in
			// the directory, then render each set once.
			DIR *d = opendir (arg);
			if (!d)
			{
				max_err = ERROR0 (ERR_CANT_OPEN, "Can't read directory: %s\n", arg);
				continue;
			}
			StringField_t bases = { 0 };
			InitializeStringField (&bases);
			struct dirent *e;
			while ((e = readdir (d)) != 0)
			{
				ccp dot = strrchr (e->d_name, '.');
				if (!dot
					|| (strcasecmp (dot, ".ncgr") && strcasecmp (dot, ".ncer")
						&& strcasecmp (dot, ".nclr") && strcasecmp (dot, ".nanr")))
					continue;
				char base[PATH_MAX];
				const uint len = (uint)(dot - e->d_name);
				if (len >= sizeof (base))
					continue;
				memcpy (base, e->d_name, len);
				base[len] = 0;
				InsertStringField (&bases, base, false);
			}
			closedir (d);

			char dir[PATH_MAX];
			const uint n = strlen (arg);
			snprintf (dir, sizeof (dir), "%s%s", arg, n && arg[n - 1] == '/' ? "" : "/");
			for (int i = 0; i < bases.used; i++)
			{
				const enumError err = sprites_from_base (dir, bases.field[i]);
				if (err != ERR_NOTHING_TO_DO && max_err < err)
					max_err = err;
			}
			ResetStringField (&bases);
		}
		else
		{
			// A single member file: derive the base name and use its directory.
			char dir[PATH_MAX], base[PATH_MAX];
			ccp slash = strrchr (arg, '/');
			ccp fname = slash ? slash + 1 : arg;
			const uint dlen = slash ? (uint)(slash - arg) + 1 : 0;
			if (dlen >= sizeof (dir))
				continue;
			memcpy (dir, arg, dlen);
			dir[dlen] = 0;
			ccp dot = strrchr (fname, '.');
			const uint blen = dot ? (uint)(dot - fname) : strlen (fname);
			if (blen >= sizeof (base))
				continue;
			memcpy (base, fname, blen);
			base[blen] = 0;

			const enumError err = sprites_from_base (dir, base);
			if (err == ERR_NOTHING_TO_DO)
				ERROR0 (ERR_WARNING, "No complete Nitro sprite set for: %s\n", arg);
			else if (max_err < err)
				max_err = err;
		}
	}
	return max_err;
}
