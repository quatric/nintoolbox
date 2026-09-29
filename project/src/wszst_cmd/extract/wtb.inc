// Extract a WTB (.wta/.wtb) texture archive: a text manifest of every texture's dimensions/
// format, plus (when a decoder for its format exists and the pixel data is reachable) a
// sibling PNG per texture. If the header points at an external .wtp, that sibling file is
// loaded automatically when present; otherwise PNG extraction is skipped and the manifest
// says so rather than guessing at a filename.
static enumError extract_wtb_manifest (ccp arg)
{
	if (!is_ext (arg, ".wta") && !is_ext (arg, ".wtb"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (!IsWTB (raw, raw_size) || raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	wtb_t wtb;
	err = ScanWTB (&wtb, raw, (uint)raw_size);
	if (err)
	{
		FREE (raw);
		return ERROR0 (ERR_INVALID_DATA, "WTB: %s: invalid texture archive\n", arg);
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".txt", false);
	else
		snprintf (dest, sizeof (dest), "%s.txt", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE WTB:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f)
			err = DecodeWTB_Text (F.f, &wtb);
		ResetFile (&F, opt_preserve);
	}

	// Figure out where the actual pixels live: this same buffer, or a sibling .wtp.
	const u8 *img_data = raw;
	uint img_size = (uint)raw_size;
	u8 *wtp_raw = 0;
	if (wtb.external_data)
	{
		char wtp_path[PATH_MAX];
		snprintf (wtp_path, sizeof (wtp_path), "%s", arg);
		char *dot = strrchr (wtp_path, '.');
		if (dot && (!strcmp (dot, ".wta") || !strcmp (dot, ".wtb")))
			strcpy (dot, ".wtp");
		else
			snprintf (wtp_path, sizeof (wtp_path), "%s.wtp", arg);

		size_t wtp_size = 0;
		if (!LoadFileAlloc (wtp_path, 0, 0, &wtp_raw, &wtp_size, 0, 0, 0, false)
			&& wtp_size <= UINT_MAX)
		{
			img_data = wtp_raw;
			img_size = (uint)wtp_size;
		}
		else
			img_data = 0; // no sibling .wtp found -- skip PNG extraction, manifest still written
	}

	if (img_data && !testmode)
	{
		char base[PATH_MAX];
		snprintf (base, sizeof (base), "%s", arg);
		char *dot = strrchr (base, '.');
		if (dot && (dot == base || dot[-1] != '/'))
			*dot = 0;

		for (uint i = 0; i < wtb.n_textures; i++)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s.%03u.png", base, i);
			if (!opt_overwrite && !access (path, F_OK))
				continue;

			u8 *rgba = 0;
			uint w = 0, h = 0;
			if (!DecodeWTB_RGBA (&rgba, &w, &h, &wtb, img_data, img_size, i) && rgba)
			{
				if (verbose >= 0)
					fprintf (stdlog, "EXTRACT WTB:%s[%u] -> PNG:%s\n", arg, i, path);
				SaveDecodedRGBAToPNG (rgba, w, h, &le_func, path, 0, opt_overwrite);
			}
		}
	}

	FREE (wtp_raw);
	ResetWTB (&wtb);
	FREE (raw);
	return err;
}
