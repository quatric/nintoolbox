///////////////		commands WC24DECRYPT/WC24ENCRYPT/BMS	///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_wc24 (bool encrypt)
{
	if (n_param < 3)
		return ERROR0 (ERR_SYNTAX,
			encrypt ? "Command WC24ENCRYPT needs: source dest key rsa-key [iv]\n"
					: "Command WC24DECRYPT needs: source dest key\n");

	ParamList_t *param = first_param;
	ccp source = param->arg;
	param = param->next;
	ccp dest = param->arg;
	param = param->next;
	ccp key = param->arg;
	param = param->next;

	if (!encrypt)
		return WC24DecryptFile (source, dest, key);

	if (!param)
		return ERROR0 (ERR_SYNTAX, "Command WC24ENCRYPT needs an RSA private key PEM\n");
	ccp rsa = param->arg;
	param = param->next;
	ccp iv = param ? param->arg : 0;
	return WC24EncryptFile (source, dest, key, rsa, iv);
}

static enumError cmd_bms (void)
{
	if (n_param < 3)
		return ERROR0 (ERR_SYNTAX, "Command BMS needs: script source dest-dir\n");

	ParamList_t *param = first_param;
	ccp script = param->arg;
	param = param->next;
	ccp source = param->arg;
	param = param->next;
	ccp outdir = param->arg;
	return RunBmsScript (script, source, outdir);
}

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
	const size_t n_head = fread (head, 1, sizeof (head), probe);
	fclose (probe);
	if (n_head < 4)
		return ERR_NOTHING_TO_DO;

	// Don't decode already decoded PNGs, XMLs, YAMLs, GLBs, etc.
	if (!memcmp (head, "\x89PNG", 4) || is_ext (arg, ".png") || is_ext (arg, ".xml")
		|| is_ext (arg, ".yaml") || is_ext (arg, ".tflyt") || is_ext (arg, ".dae")
		|| is_ext (arg, ".glb"))
		return ERR_NOTHING_TO_DO;

	bool is_image = false;
	const file_format_t fform = GetByMagicFF (head, (uint)n_head, 0);
	const nfmt_info_t nfmt = DetectNintendoFormat (head, (uint)n_head, arg);

	if (!memcmp (head, "RGCN", 4) || !memcmp (head, "NCGR", 4) || is_ext (arg, ".ncgr"))
		is_image = true;
	else if (!memcmp (head, "RLCN", 4) || !memcmp (head, "NCLR", 4) || is_ext (arg, ".nclr"))
		is_image = true;
	else if (!memcmp (head, "RECN", 4) || !memcmp (head, "NCER", 4) || is_ext (arg, ".ncer"))
		is_image = true;
	else if (!memcmp (head, "RNAN", 4) || !memcmp (head, "NANR", 4) || is_ext (arg, ".nanr"))
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
	else if (fform == FF_GVR || is_ext (arg, ".gvr"))
		is_image = true;
	else if (nfmt.type == NFMT_BFLIM || nfmt.type == NFMT_BCLIM || is_ext (arg, ".bflim")
		|| is_ext (arg, ".bclim"))
		is_image = true;
	else if (fform == FF_TPL || is_ext (arg, ".tpl"))
		is_image = true;
	else if (fform == FF_TEX || is_ext (arg, ".tex0"))
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
		const enumError load_err = LoadIMG (&img, true, arg, image_index, false, true, false);
		if (load_err)
			continue;
		char sub_dest[PATH_MAX];
		snprintf (sub_dest, sizeof (sub_dest), "%s/image_%03u.png", dir_dest, image_index);
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

//
///////////////////////////////////////////////////////////////////////////////
///////////////		    command BCH				///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_bch (void)
{
	if (!n_param)
		return ERROR0 (ERR_SYNTAX, "Command BCH needs a file\n");

	enumError max_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];
		u8 *data = 0;
		size_t fsize = 0;
		enumError err = LoadFileAlloc (arg, 0, 0, &data, &fsize, 0, 0, 0, false);
		if (err)
		{
			if (max_err < err)
				max_err = err;
			continue;
		}

		// CGFX/BCRES is the other 3DS graphics container; list it here too so
		// one command covers both rather than making the user pick.
		if (fsize >= 4 && !memcmp (data, "CGFX", 4))
		{
			cgfx_t cgfx;
			if (!ScanCGFX (&cgfx, data, (size_t)fsize))
			{
				FREE (data);
				ERROR0 (ERR_INVALID_DATA, "Invalid CGFX file: %s\n", arg);
				if (max_err < ERR_INVALID_DATA)
					max_err = ERR_INVALID_DATA;
				continue;
			}
			printf ("\n%s%s%s\n", colout->heading, arg, colout->reset);
			printf ("  CGFX/BCRES, revision 0x%08x\n", cgfx.revision);
			uint tot = 0;
			for (int i = 0; i < CGFX_N_DICTS; i++)
			{
				if (!cgfx.dict[i].n)
					continue;
				tot += cgfx.dict[i].n;
				printf ("  %-21s %u\n", GetCGFXDictName (i), cgfx.dict[i].n);
				for (uint k = 0; k < cgfx.dict[i].n; k++)
					printf ("      @%08x  %s\n", cgfx.dict[i].entries[k].address,
						cgfx.dict[i].entries[k].name);
			}
			if (!tot)
				printf ("  (no named content)\n");
			ResetCGFX (&cgfx);
			FREE (data);
			continue;
		}

		bch_t bch;
		err = ScanBCH (&bch, data, (uint)fsize);
		FREE (data);
		if (err)
		{
			ERROR0 (ERR_INVALID_DATA, "Not a BCH or CGFX file: %s\n", arg);
			if (max_err < ERR_INVALID_DATA)
				max_err = ERR_INVALID_DATA;
			continue;
		}

		printf ("\n%s%s%s\n", colout->heading, arg, colout->reset);
		printf ("  version: backward 0x%02x, forward 0x%02x\n", bch.bc, bch.fc);
		printf ("  sections: contents 0x%x+0x%x, strings 0x%x+0x%x,"
				" commands 0x%x+0x%x, raw 0x%x+0x%x, reloc 0x%x+0x%x\n",
			bch.contents_addr, bch.contents_len, bch.strings_addr, bch.strings_len,
			bch.commands_addr, bch.commands_len, bch.raw_data_addr, bch.raw_data_len,
			bch.reloc_addr, bch.reloc_len);

		uint total = 0;
		for (int i = 0; i < BCH_N_DICTS; i++)
		{
			const bch_dict_t *d = bch.dict + i;
			if (!d->n)
				continue;
			total += d->n;
			printf ("  %-21s %u\n", GetBCHDictName (i), d->n);
			for (uint k = 0; k < d->n; k++)
				printf ("      @%08x  %s\n", d->entries[k].address, d->entries[k].name);
		}
		if (!total)
			printf ("  (no named content)\n");

		ResetBCH (&bch);
	}

	ResetStringField (&plist);
	return max_err;
}

// Process a single file with the full XX pipeline: external pass-through
// (strong, then weak) first, then the native Nintendo codecs, and finally
// the regular SZS/U8/BRRES extractor.  When a pass-through tool unpacks a
// container into a staging directory, the staged tree is walked recursively
// so every leaf is peeled in turn.  Returns the strongest error seen, or
// ERR_NOTHING_TO_DO when nothing claimed the file.
static enumError extract_one_file_inner (ccp arg, ccp basedir, uint depth);

static enumError extract_one_file (ccp arg, ccp basedir, uint depth)
{
	enumError err = extract_one_file_inner (arg, basedir, depth);
	if (depth > 0 && err > ERR_WARNING)
		return ERR_WARNING;
	return err;
}

static enumError extract_one_file_inner (ccp arg, ccp basedir, uint depth)
{
	char pbase_buf[PATH_MAX] = "";
	ccp pbase = opt_dest && *opt_dest ? opt_dest : basedir;
	if (pbase)
	{
		snprintf (pbase_buf, sizeof (pbase_buf), "%s", pbase);
		const uint n = strlen (pbase_buf);
		if (n && pbase_buf[n - 1] != '/' && n + 1 < sizeof (pbase_buf))
		{
			pbase_buf[n] = '/';
			pbase_buf[n + 1] = 0;
		}
		pbase = pbase_buf;
	}

	// This manifest is emitted by every native archive extraction. It is an
	// input to CREATE, not another container to peel during recursive XX.
	ccp arg_name = strrchr (arg, '/');
	arg_name = arg_name ? arg_name + 1 : arg;
	if (!strcmp (arg_name, "wszst-setup.txt"))
		return ERR_OK;
	if (depth && (!strncmp (arg_name, "image.", 6) || !strncmp (arg_name, "mipmap-", 7)))
		return ERR_OK;

	// ExciteBots font textures can begin with 00 01 00 01, which is also a
	// plausible object-flow version/count pair. Extension plus the strict
	// texture-header/size validation must therefore get first refusal before
	// generic format/pass-through probes classify them as OBFLOW.
	enumError err = extract_tex_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_art_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	err = extract_zip_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	// Containers the main tools cannot open natively are passed through
	// to external unpackers (wit, ndstool, ctrtool, sharpii).  The strong
	// variant runs FIRST, claimed by header signature only: it neither
	// reads the whole file (so disc images above --max-file-size reach
	// wit instead of failing the native probes) nor steals any file the
	// native decoders can handle (extension-only claims are left below).
	if (!opt_no_passthrough)
	{
		char staged_dir[PATH_MAX] = "";
		enumError pas_err = PassthruExtractStrong (arg, pbase, staged_dir, sizeof (staged_dir));
		if (pas_err != ERR_NOTHING_TO_DO)
			return pas_err == ERR_OK && *staged_dir ? extract_tree_complete (staged_dir, depth + 1)
													: pas_err;
	}

	err = extract_sarc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_storybook_one_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_bigf_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;
	if (is_littlest_pet_shop_rpk(arg))
		return ERR_OK;

	err = extract_abe_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_gfa_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_rst_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_arika_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_thp_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_namco_sszl_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_namco_vcra_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_arcv_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_pac_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_gpkg_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_cod_pak0_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_gpak_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_bns_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_rpak_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_jarc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_lspk_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_fsys_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_warc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_sfzdat_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_bg4_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_cram_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_sa01_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_hwlegends_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_sze_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_rflres_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_mii_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_msh_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_hsf_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_mod_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_nud_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_numshb_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_nut_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_dtls_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractXPCKArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractZTABArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractMDRArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractPVOLArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractSTPKArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractF9ResArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractZLARCArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractGARArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractMKGPDXPacArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractTMPKArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractNXARCArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractAPAKArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractPKZArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = ExtractVIBSArchive (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_nccarc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_ccf_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_sequence_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = decode_romc_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_darc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_sar_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_narc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_mpbin_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_at7_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_ctpk_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_gsh_shaders (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_nitro_sprite_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_cfnt_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = decode_cfnt_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = decode_brfnt_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = decode_bflyt_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = decode_byml_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = export_kcl_glb_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = decode_msbt_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_ue4_pak_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_smash_arc_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_prc_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_cnut_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_xmsg_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_nwr_levelinfo_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_nwr_animtiles_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_nsmbw_chk_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_nsmbw_tex_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_kpbin_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_chans_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	// Last of the archive scanners: Metroid: Samus Returns has no magic at
	// all, so it only gets a look after every signature-bearing format has
	// declined the file.
	err = extract_metroidsr_file (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = decode_image_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	// Covers a standalone .bfres passed directly as a single argument (not
	// reached by extract_bfres_textures_tree()'s pre-pass, which only walks
	// directory/archive-shaped extractions). Always returns ERR_NOTHING_TO_DO
	// -- see its own comment for why it must not short-circuit this chain.
	extract_bfres_textures (arg, basedir, depth);
	extract_bcres_textures (arg, basedir, depth);
	// The HAL .dat/.usd probes are different: they DO claim the file. Those
	// archives are not directory-style containers, so once identified there is
	// nothing left below for the chain to do -- and the generic SZS/U8
	// extractor at the end would only fail them with ERR_FILE_ALREADY_EXISTS.
	err = extract_hsd_textures (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_halbank_textures (arg, basedir, depth);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = export_model_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = extract_bfres_switch_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	err = convert_brsar_if_possible (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;

	// A native archive's extracted BRSUB members (TEX0/PLT0/CHR0/etc., and
	// MDL0 while the outer two-pass export is deferred) are already the leaf
	// resources we want. Do not cut those detached leaves again when walking
	// an archive's freshly completed output directory.
	{
		// GetByMagicFF deliberately requires at least 0x20 bytes before it
		// classifies BRSUB resources; shorter probes avoid mistaking the small
		// extraction metadata fragments for real TEX0/MDL0/etc. records.
		u8 head[0x40];
		FILE *f = fopen (arg, "rb");
		const size_t got = f ? fread (head, 1, sizeof (head), f) : 0;
		if (f)
			fclose (f);
		const file_format_t leaf_ff = GetByMagicFF (head, (uint)got, 0);
		if ((got >= 0x20 && IsBRSUB (leaf_ff)) || leaf_ff == FF_PLT0 || leaf_ff == FF_PNG
			|| leaf_ff == FF_BREFT_IMG || (GetAttribFF (leaf_ff) & FFT_TEXT))
			return ERR_OK;
	}

	// Raw Nintendo codecs are not U8/SZS archives, but users expect the
	// regular extraction front end to handle them as a single extracted file.
	// This also makes a compressed asset found inside an extracted tree usable
	// without switching to a separate command.
	char decompressed_path[PATH_MAX];
	err = decompress_nintendo_file2 (arg, decompressed_path, sizeof (decompressed_path));
	if (err == ERR_OK)
	{
		// Every check above (SARC/GFA/PAC/DARC, BRFNT/BFLYT auto-decode,
		// model export) ran against the still-compressed bytes and correctly
		// declined. decompress_nintendo_file2() wrote the decompressed
		// payload to its own destination path (== 'arg' only when 'arg'
		// already lives under the --dest tree; a genuinely different path
		// otherwise -- recursing on 'arg' itself would just re-read the
		// still-compressed source forever and hit the depth cap). Re-run the
		// same dispatch chain against that destination now that it might
		// actually be one of those -- confirmed necessary on a real retail
		// 3DS disc, where every DARC-bundled layout family ships
		// LZ11-compressed (extract_darc_file() never saw a "darc" magic
		// before this, only the compressed bytes in front of it). The
		// decompressed payload can never itself start with the same codec's
		// magic byte again, so this terminates in one extra hop in practice;
		// the depth cap only guards against a corrupt/adversarial file.
		const enumError sub_err = depth < 32
			? extract_one_file (decompressed_path, basedir, depth + 1)
			: ERR_FILE_TOO_BIG;

		// If nothing further recognised the decompressed payload as a real
		// container (extract_one_file() left it sitting there untouched,
		// same file it wrote above), it's just an inert dump kept for
		// inspection -- the same class of bug as extract_mii_file()'s
		// glb/png previews: leaving it on disk lets a later CREATE sweep it
		// into the rebuilt archive as bogus new content. Discard it unless
		// the caller explicitly asked to keep raw dumps.
		if (!opt_export_raw)
		{
			struct stat st_leftover;
			if (!stat (decompressed_path, &st_leftover) && S_ISREG (st_leftover.st_mode))
				unlink (decompressed_path);
		}
		return sub_err;
	}
	if (err != ERR_NOTHING_TO_DO)
		return err;

	// Extension-only pass-through claims (.nds/.cia/.3ds/.cci/.cxi/
	// .wad/.app) run only after every native probe declined the file, so
	// a container misnamed as, say, a WAD never masks a real SZS archive.
	if (!opt_no_passthrough)
	{
		char staged_dir[PATH_MAX] = "";
		enumError pas_err = PassthruExtract (arg, pbase, staged_dir, sizeof (staged_dir));
		if (pas_err != ERR_NOTHING_TO_DO)
			return pas_err == ERR_OK && *staged_dir ? extract_tree_complete (staged_dir, depth + 1)
													: pas_err;
	}

	if (depth > 0)
	{
		// +1 and zeroed: GetByMagicFF() includes text-format sniffing (e.g.
		// Wavefront OBJ) that scans for a terminator/delimiter byte -- an
		// un-terminated 8-byte probe let that scan run past this buffer
		// (ASAN stack-buffer-overflow, real SSBB data reliably triggers it;
		// GetByMagicFF() itself is now also bounds-checked, but keeping this
		// probe NUL-terminated is a cheap second guard against the same
		// class of bug in any future heuristic added there).
		u8 head[9] = { 0 };
		FILE *probe = fopen (arg, "rb");
		size_t n = probe ? fread (head, 1, sizeof (head) - 1, probe) : 0;
		if (probe)
			fclose (probe);
		bool is_arch = false;
		if (n >= 4)
		{
			const file_format_t ff = GetByMagicFF (head, (uint)n, 0);
			if (IsArchiveFF (ff) || IsBRSUB (ff) || ff == FF_YAZ0 || ff == FF_YAZ1
				|| !memcmp (head, "Yaz0", 4) || !memcmp (head, "Yaz1", 4)
				|| !memcmp (head, "SARC", 4) || !memcmp (head, "NARC", 4)
				|| !memcmp (head, "CRAN", 4) || !memcmp (head, "CTPK", 4) || is_ext (arg, ".szs")
				|| is_ext (arg, ".arc") || is_ext (arg, ".carc") || is_ext (arg, ".sarc")
				|| is_ext (arg, ".bfma") || is_ext (arg, ".narc") || is_ext (arg, ".ctpk")
				|| is_ext (arg, ".pack") || is_ext (arg, ".barc") || is_ext (arg, ".rarc")
				|| is_ext (arg, ".brres") || is_ext (arg, ".bres")
				|| is_ext (arg, ".breff") || is_ext (arg, ".breft")
				|| !memcmp (head, "REFF", 4) || !memcmp (head, "REFT", 4))
				is_arch = true;
		}
		if (!is_arch)
			return ERR_OK;
	}

	szs_file_t szs;
	InitializeSZS (&szs);
	err = LoadCreateSZS (&szs, arg, true, opt_ignore > 0, false);
	if (err)
	{
		ResetSZS (&szs);

		// Last-resort fallback, tried only once the real U8/SARC/etc. parser
		// above has declined the file outright: an otherwise-opaque blob may
		// still carry self-describing embedded resources worth pulling out
		// (see extract_embedded_nw4r_file()'s own comment). This must run
		// AFTER LoadCreateSZS(), not before -- a legitimate U8 archive (e.g.
		// a Wii layout .arc bundling a BRLYT/BRLAN pair as regular SARC
		// members) contains exactly the byte patterns this scanner looks
		// for, so running it first stole real archives -- including their
		// textures -- and left only the two raw layout blobs it knows how
		// to name, silently dropping everything else in the archive.
		enumError nw4r_err = extract_embedded_nw4r_file (arg, basedir, depth);
		if (nw4r_err != ERR_NOTHING_TO_DO)
			return nw4r_err;

		return depth > 0 ? ERR_OK : err;
	}
	if (!err)
	{
		DASSERT (!szs.file_size || szs.file_size >= szs.size);

		if (szs.size >= 4 && !memcmp (szs.data, "SARC", 4))
		{
			err = extract_sarc_mem (arg, basedir, depth, szs.data, szs.size);
			ResetSZS (&szs);
			return err;
		}
		if (szs.size >= 4 && !memcmp (szs.data, "CTPK", 4))
		{
			err = extract_ctpk_mem (arg, basedir, depth, szs.data, szs.size);
			ResetSZS (&szs);
			return err;
		}
		if (szs.size >= 4 && (!memcmp (szs.data, "NARC", 4) || !memcmp (szs.data, "CRAN", 4)))
		{
			err = extract_narc_mem (arg, basedir, depth, szs.data, szs.size);
			ResetSZS (&szs);
			return err;
		}

		if (analyze_fname && IsBRSUB (szs.fform_arch))
			AnalyzeBRSUB (&szs, szs.data, szs.size, arg);

		if (depth > 0 && !IsArchiveFF (szs.fform_arch) && !IsBRSUB (szs.fform_arch))
		{
			ResetSZS (&szs);
			return ERR_OK;
		}

		have_patch_count -= 1000000;
		PRINT ("EXTRACT/%s[%s]: %s\n", __FUNCTION__, GetNameFF_SZS (&szs), szs.fname);
		err = ExtractFilesSZS (&szs, 0, false, 0, basedir);
		have_patch_count += 1000000;
		if (err <= ERR_WARNING)
		{
			// ExtractFilesSZS writes nested archives as ordinary files. Peel that
			// completed output immediately; relying on readdir() to notice a new
			// sibling directory is filesystem/order dependent and missed 1,700
			// ACCF MDL0s stored inside the Str/Brres archives.
			char dest[PATH_MAX];
			get_extract_dest (dest, sizeof (dest), &szs);
			ccp saved_dest = opt_dest;
			const int saved_export_count = export_count;
			opt_dest = 0;
			export_count = 0;
			enumError sub_err = extract_tree (dest, depth + 1);
			export_count = saved_export_count;
			opt_dest = saved_dest;
			if (err < sub_err)
				err = sub_err;
		}
		if (err <= ERR_WARNING && export_count > 0)
		{
			char dest[PATH_MAX];
			get_extract_dest (dest, sizeof (dest), &szs);
			ccp saved_dest = opt_dest;
			opt_dest = 0; // each exported model belongs beside its own source
			// BFRES FTEX textures need to already be on disk before the search
			// root is indexed below -- see extract_bfres_textures_tree()'s own
			// comment for why this can't just be folded into export_models_
			// tree()'s per-file loop like the other decoders there.
			extract_bfres_textures_tree (dest, 0);
			// Even a single BRRES can declare run-time/EFB texture resources that
			// have no TEX0 payload. Enable the completed archive directory as the
			// lookup root so COLLADA only emits images that were actually decoded.
			SetDAETextureSearchRoot (dest);
			enumError model_err = export_models_tree (dest, 0);
			SetDAETextureSearchRoot (0);
			opt_dest = saved_dest;
			if (err < model_err)
				err = model_err;
		}
		if (err <= ERR_WARNING && OptionUsed[OPT_AUTO])
		{
			char dest[PATH_MAX];
			get_extract_dest (dest, sizeof (dest), &szs);
			ccp saved_dest = opt_dest;
			opt_dest = 0; // each decoded file belongs beside its own source
			enumError auto_err = auto_decompress_tree (dest, 0);
			opt_dest = saved_dest;
			if (err < auto_err)
				err = auto_err;
		}
	}
	ResetSZS (&szs);
	return depth && err ? ERR_OK : err;
}

// True when PATH is a sibling extraction output: a directory named "<stem>.d"
// whose "<stem>" is a regular file next to it.  The native extractors write
// every decoded archive into a "<source>.d" folder beside its source, and
// those artifacts must not be re-fed into the XX pipeline (a 0-byte
// ".BMG.header" inside a ".bmg.d" was being resubmitted as a BMG, tripping
// the BMG text writer).  Pass-through stage dirs (wit's "<game>.d") have no
// sibling regular file, so this never skips the tree we are told to walk.
static bool is_extractor_output_dir (ccp path)
{
	const uint n = strlen (path);
	if (n < 3 || path[n - 1] != 'd' || path[n - 2] != '.')
		return false;

	char stem[PATH_MAX];
	snprintf (stem, sizeof (stem), "%.*s", (int)(n - 2), path);
	struct stat st;
	if (lstat (stem, &st) == 0 && S_ISREG (st.st_mode))
		return true;

	// U8/RARC/PACK destinations use the source stem (foo.arc -> foo.d), not
	// the complete source filename (foo.arc.d). Recognize any regular sibling
	// named "<stem>.<extension>" so a live parent readdir never feeds the
	// output directory a second time after extract_one_file() already peeled
	// it explicitly above.
	char parent[PATH_MAX], base[PATH_MAX];
	ccp slash = strrchr (stem, '/');
	if (slash)
	{
		snprintf (parent, sizeof (parent), "%.*s", (int)(slash - stem), stem);
		snprintf (base, sizeof (base), "%s", slash + 1);
	}
	else
	{
		snprintf (parent, sizeof (parent), ".");
		snprintf (base, sizeof (base), "%s", stem);
	}
	DIR *dir = opendir (parent);
	if (!dir)
		return false;
	const size_t base_len = strlen (base);
	bool found = false;
	struct dirent *de;
	while (!found && (de = readdir (dir)))
	{
		if (strncmp (de->d_name, base, base_len) || de->d_name[base_len] != '.'
			|| !strcmp (de->d_name + base_len, ".d"))
			continue;
		char candidate[PATH_MAX];
		snprintf (candidate, sizeof (candidate), "%s/%s", parent, de->d_name);
		found = !lstat (candidate, &st) && S_ISREG (st.st_mode);
	}
	closedir (dir);
	return found;
}

// Recurse into a directory produced by an external unpacker, peeling every
// regular file found below it exactly like a top-level XX argument.  Each
// decoded file lands beside its own source in the staged tree so the project
// tree stays deterministic for rebuilds.  Sub-directories created by the
// native extractors while unpacking ("<source>.d") are skipped so the walk
// only ever sees the tree the external tool produced.
static enumError extract_tree (ccp root, uint depth)
{
	if (depth > 32)
		return ERR_FILE_TOO_BIG;
	DIR *dir = opendir (root);
	if (!dir)
		return ERR_NOT_EXISTS;
	enumError max_err = ERR_OK;
	struct dirent *de;
	ccp saved_dest = opt_dest;
	opt_dest = 0; // enforce tree-local destinations during tree walk
	while ((de = readdir (dir)))
	{
		if (de->d_name[0] == '.')
			continue;
		char path[PATH_MAX];
		const int len = snprintf (path, sizeof (path), "%s/%s", root, de->d_name);
		if (len < 0 || (uint)len >= sizeof (path))
		{
			max_err = ERR_FILE_TOO_BIG;
			continue;
		}
		struct stat st;
		if (lstat (path, &st))
		{
			if (max_err < ERR_NOT_EXISTS)
				max_err = ERR_NOT_EXISTS;
			continue;
		}
		enumError err = ERR_OK;
		if (S_ISDIR (st.st_mode))
		{
			if ( !is_extractor_output_dir (path)
			  && ( opt_with_update_part || strcasecmp (de->d_name, "UPDATE") ) )
			{
				err = extract_tree (path, depth + 1);
			}
		}
		else if (S_ISREG (st.st_mode))
		{
			err = extract_one_file (path, 0, depth + 1);
		}
		if (err != ERR_NOTHING_TO_DO)
		{
			if (err > ERR_WARNING)
				err = ERR_WARNING;
			if (max_err < err)
				max_err = err;
		}
	}
	closedir (dir);
	opt_dest = saved_dest;
	return max_err;
}

static enumError cmd_extract (enumCommands mode)
{
	ccp basedir = GetOptBasedir ();
	if (mode == CMD_XDECODE)
	{
		RegisterOptionByIndex (&InfoUI_wszst, OPT_DECODE, 1, false);
		opt_decode = true;
	}
	else if (mode == CMD_XEXPORT)
	{
		RegisterOptionByIndex (&InfoUI_wszst, OPT_DECODE, 1, false);
		opt_decode = true;
		RegisterOptionByIndex (&InfoUI_wszst, OPT_EXPORT, 1, false);
		export_count++;
	}
	else if (mode == CMD_XALL)
	{
		RegisterOptionByIndex (&InfoUI_wszst, OPT_ALL, 1, false);
		opt_recurse = INT_MAX;
		opt_decode = true;
		opt_mipmaps = 1;
	}
	else if (mode == CMD_XCOMMON)
	{
		opt_recurse = INT_MAX;
		opt_decode = false;
		basedir = "common/";
	}

	if (opt_dest && !*opt_dest)
		opt_dest = 0;
	opt_mkdir = true;

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);
	enumError max_err = ERR_OK;

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		// XX historically recursed only into directories produced by an external
		// pass-through extractor.  Treat a directory supplied on the command line
		// the same way, so `wszst XX path/to/tree` peels every regular file below
		// it while still skipping sibling "<archive>.d" output directories.
		enumError err;
		if (IsDirectory (arg, false) && export_count > 0)
		{
			err = extract_tree_complete (arg, 0);
		}
		else
			err = IsDirectory (arg, false) ? extract_tree (arg, 0)
										   : extract_one_file (arg, basedir, 0);
		if (max_err < err)
			max_err = err;
	}

	ResetStringField (&plist);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			convert helper			///////////////
///////////////////////////////////////////////////////////////////////////////

static bool ConvertHelper (const u8 *data, // valid data
	uint data_size, // size of 'data'
	ContainerData_t *cdata, // NULL or container data
	ccp src_fname, // filename of source
	ccp dest_fname, // filename of destination
	bool binary_dest, // output: false:text, true:binary
	enumError *err // not NULL: store error code here
)
{
	enumError temp_err;
	if (!err)
		err = &temp_err;
	*err = ERR_OK;

	// [[analyse-magic]]
	const file_format_t fform = GetByMagicFF (data, data_size, data_size);
	switch (fform)
	{
		case FF_BMG:
		case FF_BMG_TXT:
			FreeContainerData (cdata);
			{
				bmg_t bmg;
				*err = ScanBMG (&bmg, true, src_fname, data, data_size);
				if (*err)
					return true;
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawXBMG (&bmg, dest_fname, true)
								  : SaveTextXBMG (&bmg, dest_fname, true);
				ResetBMG (&bmg);
			}
			return true;

		case FF_MSBT:
		case FF_MSBT_TXT:
			FreeContainerData (cdata);
			{
				if (fform == FF_MSBT)
				{
					msbt_file_t msbt;
					*err = ScanMSBT (&msbt, data, data_size, src_fname);
					if (!*err && !testmode)
						*err = SaveTextMSBT (&msbt, dest_fname);
					ResetMSBT (&msbt);
				}
				else
				{
					msbt_file_t msbt;
					*err = LoadTextMSBT (&msbt, src_fname);
					if (!*err && !testmode)
					{
						u8 *out_data = 0;
						uint out_size = 0;
						*err = CreateMSBT (&out_data, &out_size, &msbt);
						if (!*err && out_data)
						{
							File_t F;
							*err = CreateFileOpt (&F, true, dest_fname, false, src_fname);
							if (F.f)
								fwrite (out_data, 1, out_size, F.f);
							ResetFile (&F, opt_preserve);
							FREE (out_data);
						}
					}
					ResetMSBT (&msbt);
				}
			}
			return true;

		case FF_MSBP:
		case FF_MSBP_TXT:
			FreeContainerData (cdata);
			{
				if (fform == FF_MSBP)
				{
					msbp_file_t msbp;
					*err = ScanMSBP (&msbp, data, data_size, src_fname);
					if (!*err && !testmode)
						*err = SaveTextMSBP (&msbp, dest_fname);
					ResetMSBP (&msbp);
				}
				else
				{
					msbp_file_t msbp;
					*err = LoadTextMSBP (&msbp, src_fname);
					if (!*err && !testmode)
					{
						u8 *out_data = 0;
						uint out_size = 0;
						*err = CreateMSBP (&out_data, &out_size, &msbp);
						if (!*err && out_data)
						{
							File_t F;
							*err = CreateFileOpt (&F, true, dest_fname, false, src_fname);
							if (F.f)
								fwrite (out_data, 1, out_size, F.f);
							ResetFile (&F, opt_preserve);
							FREE (out_data);
						}
					}
					ResetMSBP (&msbp);
				}
			}
			return true;

		case FF_MSBF:
		case FF_MSBF_TXT:
			FreeContainerData (cdata);
			{
				if (fform == FF_MSBF)
				{
					msbf_file_t msbf;
					*err = ScanMSBF (&msbf, data, data_size, src_fname);
					if (!*err && !testmode)
						*err = SaveTextMSBF (&msbf, dest_fname);
					ResetMSBF (&msbf);
				}
				else
				{
					msbf_file_t msbf;
					*err = LoadTextMSBF (&msbf, src_fname);
					if (!*err && !testmode)
					{
						u8 *out_data = 0;
						uint out_size = 0;
						*err = CreateMSBF (&out_data, &out_size, &msbf);
						if (!*err && out_data)
						{
							File_t F;
							*err = CreateFileOpt (&F, true, dest_fname, false, src_fname);
							if (F.f)
								fwrite (out_data, 1, out_size, F.f);
							ResetFile (&F, opt_preserve);
							FREE (out_data);
						}
					}
					ResetMSBF (&msbf);
				}
			}
			return true;

		case FF_GH_ITEM:
		case FF_GH_IOBJ:
		case FF_GH_KART:
		case FF_GH_KOBJ:
		case FF_GH_ITEM_TXT:
		case FF_GH_IOBJ_TXT:
		case FF_GH_KART_TXT:
		case FF_GH_KOBJ_TXT:
			FreeContainerData (cdata);
			{
				geohit_t geohit;
				InitializeGEOHIT (&geohit, fform);
				geohit.fname = src_fname;
				*err = ScanGEOHIT (&geohit, false, data, data_size);
				if (*err)
					return false;
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawGEOHIT (&geohit, dest_fname, true)
								  : SaveTextGEOHIT (&geohit, dest_fname, true, minimize_level);
				geohit.fname = 0;
				ResetGEOHIT (&geohit);
			}
			return true;

		case FF_KCL:
		case FF_KCL_TXT:
		case FF_WAV_OBJ:
		case FF_SKP_OBJ:
			FreeContainerData (cdata);
			{
				kcl_t kcl;
				InitializeKCL (&kcl);
				kcl.fname = src_fname;
				*err = ScanKCL (&kcl, false, data, data_size, true, global_check_mode);
				if (*err)
					return false;
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawKCL (&kcl, dest_fname, true)
								  : SaveTextKCL (&kcl, dest_fname, true);
				kcl.fname = 0;
				ResetKCL (&kcl);
			}
			return true;

		case FF_ITEMSLT:
		case FF_ITEMSLT_TXT:
			FreeContainerData (cdata);
			{
				itemslot_t itemslot;
				InitializeITEMSLOT (&itemslot, fform);
				itemslot.fname = src_fname;
				*err = ScanITEMSLOT (&itemslot, false, data, data_size, src_fname);
				if (*err)
					return false;
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawITEMSLOT (&itemslot, dest_fname, true)
								  : SaveTextITEMSLOT (&itemslot, dest_fname, true, minimize_level);
				itemslot.fname = 0;
				ResetITEMSLOT (&itemslot);
			}
			return true;

		case FF_KMG:
		case FF_KMG_TXT:
			FreeContainerData (cdata);
			{
				minigame_t minigame;
				InitializeMINIGAME (&minigame, fform);
				minigame.fname = src_fname;
				*err = ScanMINIGAME (&minigame, false, data, data_size);
				if (*err)
					return false;
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawMINIGAME (&minigame, dest_fname, true)
								  : SaveTextMINIGAME (&minigame, dest_fname, true, minimize_level);
				minigame.fname = 0;
				ResetMINIGAME (&minigame);
			}
			return true;

		case FF_KMP:
		case FF_KMP_TXT:
			FreeContainerData (cdata);
			{
				kmp_t kmp;
				InitializeKMP (&kmp);
				kmp.fname = src_fname;
				*err = ScanKMP (&kmp, false, data, data_size, global_check_mode);
				if (*err)
					return false;
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawKMP (&kmp, dest_fname, true)
								  : SaveTextKMP (&kmp, dest_fname, true);
				kmp.fname = 0;
				ResetKMP (&kmp);
			}
			return true;

		case FF_LEX:
		case FF_LEX_TXT:
			FreeContainerData (cdata);
			{
				lex_t lex;
				InitializeLEX (&lex);
				lex.fname = src_fname;
				*err = ScanLEX (&lex, false, data, data_size);
				if (*err)
					return false;
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawLEX (&lex, dest_fname, true)
								  : SaveTextLEX (&lex, dest_fname, true);
				lex.fname = 0;
				ResetLEX (&lex);
			}
			return true;

		case FF_MDL:
		case FF_MDL_TXT: // [[2do]] add format to UI commands BINARY and TEXT
		{
			mdl_t mdl;
			InitializeMDL (&mdl);
			mdl.fname = src_fname;
#if USE_NEW_CONTAINER_MDL
			*err = ScanMDL (&mdl, false, data, data_size, cdata, global_check_mode);
#else
			*err = ScanMDL (&mdl, false, data, data_size, 0, global_check_mode);
#endif
			if (*err)
				return false;
			*err = testmode	  ? ERR_OK
				: binary_dest ? SaveRawMDL (&mdl, dest_fname, true)
							  : SaveTextMDL (&mdl, dest_fname, true);
			mdl.fname = 0;
			ResetMDL (&mdl);
		}
			return true;

		case FF_OBJFLOW:
		case FF_OBJFLOW_TXT:
			FreeContainerData (cdata);
			{
				objflow_t objflow;
				InitializeOBJFLOW (&objflow);
				objflow.fname = src_fname;
				*err = ScanOBJFLOW (&objflow, false, data, data_size);
				if (*err)
					return false;
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawOBJFLOW (&objflow, dest_fname, true)
								  : SaveTextOBJFLOW (&objflow, dest_fname, true, minimize_level);
				objflow.fname = 0;
				ResetOBJFLOW (&objflow);
			}
			return true;

		case FF_PAT:
		case FF_PAT_TXT:
		{
			pat_t pat;
			InitializePAT (&pat);
			pat.fname = src_fname;
#if USE_NEW_CONTAINER_PAT
			*err = ScanPAT (&pat, false, data, data_size, cdata, global_check_mode);
#else
			*err = ScanPAT (&pat, false, data, data_size, 0, global_check_mode);
#endif
			if (*err)
				return false;
			*err = testmode	  ? ERR_OK
				: binary_dest ? SaveRawPAT (&pat, dest_fname, true)
							  : SaveTextPAT (&pat, dest_fname, true);
			pat.fname = 0;
			ResetPAT (&pat);
		}
			return true;

		case FF_CHR:
		case FF_CHR_TXT:
			FreeContainerData (cdata);
			{
				chr0_t chr;
				InitializeCHR0 (&chr);
				*err = fform == FF_CHR_TXT ? ScanTextCHR0 (&chr, false, src_fname)
										   : ScanRawCHR0 (&chr, false, data, data_size);
				if (*err)
				{
					ResetCHR0 (&chr);
					return false;
				}
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawCHR0 (&chr, dest_fname, true)
								  : SaveTextCHR0 (&chr, dest_fname, true);
				ResetCHR0 (&chr);
			}
			return true;

		case FF_SRT:
		case FF_SRT_TXT:
			FreeContainerData (cdata);
			{
				srt0_t srt;
				InitializeSRT0 (&srt);
				*err = fform == FF_SRT_TXT ? ScanTextSRT0 (&srt, false, src_fname)
										   : ScanRawSRT0 (&srt, false, data, data_size);
				if (*err)
				{
					ResetSRT0 (&srt);
					return false;
				}
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawSRT0 (&srt, dest_fname, true)
								  : SaveTextSRT0 (&srt, dest_fname, true);
				ResetSRT0 (&srt);
			}
			return true;

		case FF_VIS:
		case FF_VIS_TXT:
			FreeContainerData (cdata);
			{
				vis0_t vis;
				InitializeVIS0 (&vis);
				*err = fform == FF_VIS_TXT ? ScanTextVIS0 (&vis, false, src_fname)
										   : ScanRawVIS0 (&vis, false, data, data_size);
				if (*err)
				{
					ResetVIS0 (&vis);
					return false;
				}
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawVIS0 (&vis, dest_fname, true)
								  : SaveTextVIS0 (&vis, dest_fname, true);
				ResetVIS0 (&vis);
			}
			return true;

		case FF_SCN:
		case FF_SCN_TXT:
			FreeContainerData (cdata);
			{
				scn0_t scn;
				InitializeSCN0 (&scn);
				*err = fform == FF_SCN_TXT ? ScanTextSCN0 (&scn, false, src_fname)
										   : ScanRawSCN0 (&scn, false, data, data_size);
				if (*err)
				{
					ResetSCN0 (&scn);
					return false;
				}
				*err = testmode   ? ERR_OK
					: binary_dest ? SaveRawSCN0 (&scn, dest_fname, true)
								  : SaveTextSCN0 (&scn, dest_fname, true);
				ResetSCN0 (&scn);
			}
			return true;

		case FF_SHP:
		case FF_SHP_TXT:
			FreeContainerData (cdata);
			{
				shp0_t shp;
				InitializeSHP0 (&shp);
				*err = fform == FF_SHP_TXT ? ScanTextSHP0 (&shp, false, src_fname)
										   : ScanRawSHP0 (&shp, false, data, data_size);
				if (*err)
				{
					ResetSHP0 (&shp);
					return false;
				}
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawSHP0 (&shp, dest_fname, true)
								  : SaveTextSHP0 (&shp, dest_fname, true);
				ResetSHP0 (&shp);
			}
			return true;

		case FF_CLR:
		case FF_CLR_TXT:
			FreeContainerData (cdata);
			{
				clr0_t clr;
				InitializeCLR0 (&clr);
				*err = fform == FF_CLR_TXT ? ScanTextCLR0 (&clr, false, src_fname)
										   : ScanRawCLR0 (&clr, false, data, data_size);
				if (*err)
				{
					ResetCLR0 (&clr);
					return false;
				}
				*err = testmode	  ? ERR_OK
					: binary_dest ? SaveRawCLR0 (&clr, dest_fname, true)
								  : SaveTextCLR0 (&clr, dest_fname, true);
				ResetCLR0 (&clr);
			}
			return true;

		case FF_BFLYT:
		case FF_BCLYT:
		case FF_BRLYT:
		case FF_BRLAN:
		case FF_BFLYT_TXT:
		case FF_BCLYT_TXT:
			FreeContainerData (cdata);
			{
				bflyt_t bflyt;
				InitializeBFLYT (&bflyt);
				*err = ScanBFLYT (&bflyt, false, data, data_size);
				if (!*err)
					*err = testmode	  ? ERR_OK
						: binary_dest ? SaveRawBFLYT (&bflyt, dest_fname, true)
									  : SaveTextBFLYT (&bflyt, dest_fname, true);
				ResetBFLYT (&bflyt);
			}
			return true;

		default:
			break;
	}

	*err = ERR_WRONG_FILE_TYPE;
	return false;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command binary/text		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_convert (bool binary) // cmd_binary() cmd_text()
{
	static ccp def_path = "\1P/\1N\1?T";
	CheckOptDest (def_path, false);
	char dest[PATH_MAX];
	enumError max_err = ERR_OK;

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
		{
			ResetRawData (&raw);
			return err;
		}

		file_format_t ff_dest = binary ? GetBinFF (raw.fform) : GetTextFF (raw.fform);

		// Handle layout formats (BFLYT / BCLYT / BRLYT / BRLAN)
		if (!ff_dest
			&& (raw.fform == FF_BFLYT || raw.fform == FF_BCLYT || raw.fform == FF_BRLYT
				|| raw.fform == FF_BRLAN))
		{
			if (binary)
				SubstDest (dest, sizeof (dest), arg, opt_dest, "\\1P/\\1N.bflyt", ".bflyt", false);
			else
				SubstDest (dest, sizeof (dest), arg, opt_dest, "\\1P/\\1N.tflyt", ".tflyt", false);
			if (verbose >= 0)
			{
				fprintf (stdlog, "%sCREATE/TEXT %s:%s => %s\n", verbose > 0 ? "\n" : "",
					GetNameFF (raw.fform, 0), arg, dest);
				fflush (stdlog);
			}
			if (!testmode)
			{
				bflyt_t bflyt;
				InitializeBFLYT (&bflyt);
				err = ScanBFLYT (&bflyt, false, raw.data, raw.data_size);
				if (!err)
					err = binary ? SaveRawBFLYT (&bflyt, dest, true)
								 : SaveTextBFLYT (&bflyt, dest, true);
				ResetBFLYT (&bflyt);
			}
			if (err && max_err < err)
				max_err = err;
			continue;
		}

		if (!ff_dest && raw.data_size >= 16
			&& (!memcmp (raw.data, "BY", 2) || !memcmp (raw.data, "YB", 2)))
		{
			SubstDest (dest, sizeof (dest), arg, opt_dest, "\\1P/\\1N.yaml", ".yaml", false);
			if (verbose >= 0)
			{
				fprintf (
					stdlog, "%sCREATE/TEXT BYML:%s => %s\n", verbose > 0 ? "\n" : "", arg, dest);
				fflush (stdlog);
			}
			if (!testmode)
			{
				File_t F;
				err = CreateFileOpt (&F, true, dest, false, arg);
				if (!F.f)
					err = ERR_WRITE_FAILED;
				else
				{
					err = DecodeBYML_YAML (F.f, raw.data, raw.data_size);
					ResetFile (&F, opt_preserve);
				}
			}
			if (err && max_err < err)
				max_err = err;
			continue;
		}

		if (!ff_dest)
		{
			ERROR0 (
				ERR_WARNING, "File format %s not supported: %s\n", GetNameFF (raw.fform, 0), arg);
			if (max_err < ERR_WARNING)
				max_err = ERR_WARNING;
			continue;
		}

		SubstDest (dest, sizeof (dest), arg, opt_dest, def_path, GetExtFF (0, ff_dest), false);

		if (verbose >= 0)
		{
			fprintf (stdlog, "%sCREATE/%s %s:%s => %s:%s\n", verbose > 0 ? "\n" : "",
				binary ? "BINARY" : "TEXT", GetNameFF (raw.fform, 0), arg, GetNameFF (ff_dest, 0),
				dest);
			fflush (stdlog);
		}

		SetupContainerRawData (&raw);
		ConvertHelper (
			raw.data, raw.data_size, LinkContainerData (&raw.container), arg, dest, binary, &err);
		if (err)
		{
			ERROR0 (err, 0);
			if (max_err < err)
				max_err = err;
		}
	}

	ResetStringField (&plist);
	ResetRawData (&raw);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command cat			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_cat ()
{
	stdlog = stderr;
	enumError max_err = ERR_OK;

	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT_SUB);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];
		szs_extract_t eszs;
		enumError err = ExtractSZS (&eszs, true, arg, 0, opt_ignore > 0);
		PRINT ("stat=%u, data=%p, found=%d,%d, path=%s\n", err, eszs.data, !eszs.subfile_found,
			eszs.szs_found, eszs.subpath);

		if (eszs.data)
		{
			if (verbose > 0 || testmode)
			{
				fprintf (stdlog, "%s%sCAT %s:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", GetNameFF (eszs.fform_file, eszs.fform_arch),
					eszs.fname);
				fflush (stdlog);
			}

			if (!testmode)
			{
				if (opt_decode)
				{
					ConvertHelper (eszs.data, eszs.data_size, LinkContainerESZS (&eszs), eszs.fname,
						"-", false, &err);
				}
				else
				{
					size_t wstat = fwrite (eszs.data, 1, eszs.data_size, stdout);
					if (wstat != eszs.data_size)
						err = ERROR1 (ERR_WRITE_FAILED, "Write to stdout failed\n");
				}
			}
		}
		else if (!err && !opt_ignore)
			err = PrintErrorExtractSZS (&eszs, arg);
		fflush (stdout);

		if (max_err < err)
			max_err = err;
		ResetExtractSZS (&eszs);
	}

	ResetStringField (&plist);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command <FFORM>			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_FFORM (file_format_t fform, ccp autoname)
{
	stdlog = stderr;
	// char pathbuf[PATH_MAX];
	enable_kcl_drop_auto++;

	enumError max_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT_SUB);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		szs_extract_t eszs;
		enumError err = ExtractSZS (&eszs, true, arg, autoname, opt_ignore > 0);

		// [[analyse-magic]]
		if (eszs.data && GetByMagicFF (eszs.data, eszs.data_size, eszs.data_size) == fform)
		{
			if (verbose > 0 || testmode)
				fprintf (stdlog, "%s%sCAT %s:%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", GetNameFF (eszs.fform_file, eszs.fform_arch),
					eszs.fname);

			if (!testmode)
				ConvertHelper (eszs.data, eszs.data_size, LinkContainerESZS (&eszs), eszs.fname,
					"-", false, &err);
		}
		else if (!err && !opt_ignore)
			err = PrintErrorExtractSZS (&eszs, arg);

		if (max_err < err)
			max_err = err;
		ResetExtractSZS (&eszs);
	}

	ResetStringField (&plist);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command info			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_info ()
{
	stdlog = stderr;

	char pathbuf[PATH_MAX];
	ccp search_tab[] = { "info.txt", "credits.txt", 0 };

	szs_extract_t eszs;
	InitializeExtractSZS (&eszs);

	enumError max_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		ccp *search;
		for (search = search_tab;; search++)
		{
			if (!*search)
			{
				if (!opt_ignore)
					printf ("\n* NO INFO FOUND: %s\n\n", arg);
				max_err = ERR_WARNING;
				break;
			}

			ccp path = PathCatPP (pathbuf, sizeof (pathbuf), arg, *search);
			ResetExtractSZS (&eszs);
			enumError err = ExtractSZS (&eszs, false, path, 0, true);
			if (!err && eszs.data)
			{
				if (print_header)
					printf ("\n* %s\n\n", eszs.fname);
				fwrite (eszs.data, 1, eszs.data_size, stdout);
				if (eszs.data_size && eszs.data[eszs.data_size - 1] != '\n')
					putchar ('\n');
				if (print_header)
					putchar ('\n');
				break;
			}
		}
	}

	ResetStringField (&plist);
	ResetExtractSZS (&eszs);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command ghost			///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_ghost ()
{
	static ccp def_path = "\1P/\1F\1?T";
	CheckOptDest (def_path, false);
	char dest[PATH_MAX];

	raw_data_t raw;
	InitializeRawData (&raw);

	rkg_info_t ri;
	InitializeRKGInfo (&ri);
	enumError max_err = ERR_OK;

	File_t fo;
	InitializeFile (&fo);

	PrintScript_t ps;
	SetupPrintScriptByOptions (&ps);

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

		if (!fo.f)
			SubstDest (
				dest, sizeof (dest), arg, opt_dest, def_path, GetExtFF (script_fform, 0), false);

		if (verbose >= 0)
		{
			fprintf (stdlog, "%sANALYZE %s:%s => %s:%s\n", verbose > 0 ? "\n" : "",
				GetNameFF (raw.fform, 0), raw.fname, GetNameFF (script_fform, 0), dest);
			fflush (stdlog);
		}

		err = ScanRawDataGHOST (&ri, false, &raw);
		if (err >= ERR_WARNING)
		{
			if (max_err < err)
				max_err = err;
			continue;
		}

		if (!fo.f)
		{
			enumError err = CreateFile (&fo, false, dest, FM_STDIO | FM_OVERWRITE);
			if (err)
			{
				max_err = err;
				break;
			}
			ps.f = fo.f;
			PrintScriptHeader (&ps);
		}

		PrintScriptVars (&ps, 3,
			"compressed=%d\n"
			"size=%u\n"
			"score=%u\n"
			"time_button=%u\n"
			"time_direction=%u\n"
			"time_trick=%u\n",
			ri.is_compressed, ri.data_size, ri.score, ri.time_but, ri.time_dir, ri.time_trick);
#if 0
	printf("score = %u, compr = %d, size = %u\n",
		ri.score, ri.is_compressed, ri.data_size );
	printf("but = %u,%u, dir = %u,%u, trick=%u,%u\n",
		ri.n_but, ri.time_but,
		ri.n_dir, ri.time_dir,
		ri.n_trick, ri.time_trick );
#endif

		if (!script_array)
		{
			PrintScriptFooter (&ps);
			ps.f = 0;
			ResetFile (&fo, 0);
		}
	}

	PrintScriptFooter (&ps);

	ResetStringField (&plist);
	ResetPrintScript (&ps);
	ResetFile (&fo, 0);
	ResetRKGInfo (&ri);
	ResetRawData (&raw);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command decompress		///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_yazdump ()
{
	CheckOptDest ("-", false);

	enumError max_err = ERR_OK;
	StringField_t plist = { 0 };
	CollectExpandParam (&plist, first_param, -1, WM__DEFAULT);

	for (int argi = 0; argi < plist.used; argi++)
	{
		ccp arg = plist.field[argi];

		szs_file_t szs;
		InitializeSZS (&szs);
		enumError err = LoadSZS (&szs, arg, false, opt_ignore > 0, false);

		if (err <= ERR_WARNING && err != ERR_NOT_EXISTS
			&& (szs.fform_file == FF_YAZ0 || szs.fform_file == FF_YAZ1))
		{
			char dest[PATH_MAX];
			SubstDest (dest, sizeof (dest), arg, opt_dest, ".txt", ".txt", false);

			File_t F;
			CreateFileOpt (&F, true, dest, testmode, 0);
			if (F.f)
			{
				if (verbose >= 0 || testmode)
					fprintf (F.f, "%sYAZ DUMP of %s:%s\n", verbose > 0 ? "\n" : "",
						GetNameFF_SZS (&szs), arg);
				err = DecompressSZS (&szs, true, F.f);
			}
		}

		if (max_err < err)
			max_err = err;
		ResetSZS (&szs);
	}

	ResetStringField (&plist);
	return max_err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			command vehicle			///////////////
///////////////////////////////////////////////////////////////////////////////

#ifndef HAVE_WIIMM_EXT

enumError cmd_vehicle ()
{
	ERROR0 (ERR_NOT_IMPLEMENTED, "Command VEHICLE not implemented in this version!\n");
	ExitFixed (ERR_NOT_IMPLEMENTED);
}

#endif
