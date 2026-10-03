static enumError extract_sarc_mem (ccp arg, ccp basedir, uint depth, const u8 *raw, size_t raw_size)
{
	if (!raw || raw_size < 4 || raw_size > UINT_MAX || memcmp (raw, "SARC", 4))
		return ERR_NOTHING_TO_DO;
	nintendo_sarc_t sarc;
	enumError err = ScanSARC (&sarc, raw, raw_size);
	if (err)
		return err;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SARC:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, sarc.n_entries, dest);
	for (uint i = 0; !err && i < sarc.n_entries; i++)
	{
		ccp name = 0;
		const u8 *data = 0;
		uint size = 0;
		err = GetSARCEntry (&sarc, i, &name, &data, &size);
		if (err)
			break;
		char auto_name[PATH_MAX];
		if (!err && (!name || !*name))
		{
			ccp ext = ".bin";
			if (size >= 4)
			{
				if (!memcmp (data, "CTPK", 4))
					ext = ".ctpk";
				else if (!memcmp (data, "CGFX", 4))
					ext = ".cgfx";
				else if (!memcmp (data, "CLYT", 4))
					ext = ".bclyt";
				else if (!memcmp (data, "CLAN", 4))
					ext = ".bclan";
				else if (!memcmp (data, "BCTR", 4))
					ext = ".bctr";
				else if (!memcmp (data, "BSEQ", 4))
					ext = ".bseq";
				else if (!memcmp (data, "DVLB", 4))
					ext = ".dvlb";
				else if (!memcmp (data, "SPBD", 4))
					ext = ".spbd";
				else if (size >= 0x28
					&& (!memcmp (data + size - 0x28, "CLIM", 4)
						|| !memcmp (data + size - 0x28, "FLIM", 4)))
					ext = ".bclim";
			}
			snprintf (auto_name, sizeof (auto_name), "file_%04u%s", i, ext);
			name = auto_name;
		}
		if (!valid_sarc_path (name))
		{
			// A named-but-unsafe entry (path traversal, or a name with
			// invalid UTF-8 bytes macOS's filesystem refuses -- see
			// valid_utf8()'s comment) used to abort every remaining entry
			// in the archive. Give it a synthetic name instead, same as
			// the "no name at all" case just above.
			snprintf (auto_name, sizeof (auto_name), "file_%04u.bin", i);
			name = auto_name;
		}
		if (testmode)
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
		const enumError close_err = ResetFile (&F, opt_preserve);
		if (close_err > err)
			err = close_err;
	}
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}

static enumError extract_sarc_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".sarc") && !is_ext (arg, ".szs") && !is_ext (arg, ".lyarc")
		&& !is_ext (arg, ".arc") && !is_ext (arg, ".pack") && !is_ext (arg, ".bin")
		&& !is_ext (arg, ".fzip") && !is_ext (arg, ".bfma"))
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
	if (raw_size >= 8 && !memcmp (raw, "FZIP", 4))
	{
		u8 *dec = 0;
		uint dec_sz = 0;
		if (DecodeFZIP (&dec, &dec_sz, raw, (uint)raw_size) == ERR_OK && dec)
		{
			FREE (raw);
			raw = dec;
			raw_size = dec_sz;
		}
	}
	err = extract_sarc_mem (arg, basedir, depth, raw, raw_size);
	FREE (raw);
	return err;
}
