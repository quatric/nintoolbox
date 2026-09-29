static enumError extract_ctpk_mem (ccp arg, ccp basedir, uint depth, const u8 *raw, size_t raw_size)
{
	if (!raw || raw_size < 0x20 || raw_size > UINT_MAX || memcmp (raw, "CTPK", 4))
		return ERR_NOTHING_TO_DO;
	nintendo_ctpk_t ctpk;
	enumError err = ScanCTPK (&ctpk, raw, raw_size);
	if (err)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CTPK:%s (%u textures) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, ctpk.n_entries, dest);

	// Everything up to texture_offset -- header, entry table, and the
	// undocumented bytes/string table between them -- is captured verbatim
	// so create_ctpk_dir() can rebuild a byte-exact archive later without
	// this code needing to understand any of it (see RebuildCTPKFromPrefix
	// in lib-ctpk.c).
	if (!testmode && CreatePath (dest, true))
		return ERROR0 (ERR_CANT_CREATE_DIR, "Cannot create dest dir: %s", dest);

	if (!testmode && ctpk.texture_offset <= raw_size)
	{
		char prefix_path[PATH_MAX];
		snprintf (prefix_path, sizeof (prefix_path), "%s/%s_ctpk_prefix.bin", dest,
			basedir ? basedir : "");
		SaveFile (prefix_path, 0, 0, raw, ctpk.texture_offset, 0);
	}

	for (uint i = 0; !err && i < ctpk.n_entries; i++)
	{
		nintendo_ctpk_entry_t entry;
		err = GetCTPKEntry (&ctpk, i, &entry);
		if (err || testmode)
			continue;

		u8 *rgba = 0;
		uint w = 0, h = 0;
		err = DecodeCTPKTexture_RGBA (&rgba, &w, &h, &entry);
		if (err)
			continue;

		char clean_base[PATH_MAX];
		if (entry.name[0])
		{
			ccp slash = strrchr (entry.name, '/');
			ccp bslash = strrchr (entry.name, '\\');
			ccp base = slash && bslash ? (slash > bslash ? slash + 1 : bslash + 1)
				: slash				   ? slash + 1
				: bslash			   ? bslash + 1
									   : entry.name;
			snprintf (clean_base, sizeof (clean_base), "%s", base);
			char *dot = strrchr (clean_base, '.');
			if (dot)
				*dot = 0;
		}
		else
		{
			snprintf (clean_base, sizeof (clean_base), "tex_%03u", i);
		}

		char sub_name[PATH_MAX];
		snprintf (sub_name, sizeof (sub_name), "%s.png", clean_base);

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s%s", dest, basedir ? basedir : "", sub_name);

		// The entry's raw (undecoded) pixel bytes, saved verbatim beside the
		// viewable PNG so a rebuild can restore them exactly instead of
		// re-encoding through DecodeCTPKTexture_RGBA/EncodeCTPK, which is
		// lossy for every block-compressed format (ETC1/ETC1A4 included).
		// The leading index ties it back to its entry regardless of name.
		char raw_path[PATH_MAX];
		snprintf (raw_path, sizeof (raw_path), "%s/%s%03u_%s.ctpktex", dest, basedir ? basedir : "",
			i, clean_base);
		SaveFile (raw_path, 0, 0, entry.data, entry.data_size, 0);

		Image_t img;
		InitializeIMG (&img);
		const uint xw = EXPAND8 (w), xh = EXPAND8 (h);
		u8 *padded = xw == w && xh == h ? rgba : CALLOC (1, xw * xh * 4);
		if (padded != rgba)
		{
			for (uint y = 0; y < h; y++)
				memcpy (padded + y * xw * 4, rgba + y * w * 4, w * 4);
			FREE (rgba);
		}
		img.data = padded;
		img.data_alloced = true;
		img.data_size = xw * xh * 4;
		img.width = w;
		img.xwidth = xw;
		img.height = h;
		img.xheight = xh;
		img.iform = img.info_iform = IMG_X_RGB;
		img.info_fform = FF_PNG;
		img.info_n_image = 1;
		img.endian = &le_func;

		err = SavePNG (&img, false, 0, out_path, 0, 0, opt_overwrite > 0, 0);
		ResetIMG (&img);
	}
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

static enumError extract_ctpk_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".ctpk") && !is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_FILE_TOO_BIG;
	}
	err = extract_ctpk_mem (arg, basedir, depth, raw, raw_size);
	FREE (raw);
	return err;
}
