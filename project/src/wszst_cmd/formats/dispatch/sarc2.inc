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

		if (szs.fform_arch == FF_SDAT || szs.fform_arch == FF_BCSAR || szs.fform_arch == FF_BFSAR)
		{
			ResetSZS (&szs);
			return depth > 0 ? ERR_OK : ERR_NOTHING_TO_DO;
		}

		if (!IsArchiveFF (szs.fform_arch) && !IsBRSUB (szs.fform_arch))
		{
			ResetSZS (&szs);
			return depth > 0 ? ERR_OK : ERR_NOTHING_TO_DO;
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
