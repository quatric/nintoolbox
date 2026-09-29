// Extract Metroid Prime Remastered PACK containers (Switch): LE RFRM
// form (PACK v1) with a TOCC v3 asset directory. Members are written
// as "<STRG name>.<TYPE>" (guid fallback) with the FOOT form appended,
// exactly retrotool's on-disk convention, so its `pak package` accepts
// this output and the TXTR decoder finds the META it needs.
static enumError extract_mpr_pack_file (ccp arg, ccp basedir, uint depth)
{
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

	mpr_pack_t pak;
	err = ScanMPRPACK (&pak, raw, (uint)raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT MPR-PACK:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, pak.n_entries, dest);

	for (uint i = 0; !err && i < pak.n_entries; i++)
	{
		const mpr_pack_entry_t *e = pak.entries + i;
		if (testmode)
			continue;

		u8 *data = 0;
		uint data_size = 0;
		err = GetMPRPACKEntry (&data, &data_size, &pak, i);
		if (err)
			break;

		// Compression mode for the AINF footer: the stored stream's own
		// u32 LE word when the entry is compressed, 0 when stored.
		uint comp_mode = 0;
		if (e->size != e->decomp_size && e->size >= 4)
			comp_mode = (uint)raw[e->offset] | (uint)raw[e->offset + 1] << 8
				| (uint)raw[e->offset + 2] << 16 | (uint)raw[e->offset + 3] << 24;

		u8 *foot = 0;
		uint foot_size = 0;
		err = BuildMPRPACKFoot (&foot, &foot_size, &pak, i, comp_mode, e->offset);
		if (err)
		{
			FREE (data);
			break;
		}

		char type[5];
		memcpy (type, e->type, 4);
		type[4] = 0;
		char leaf[PATH_MAX];
		ccp name = FindMPRPACKName (&pak, e->guid);
		if (name && *name)
			snprintf (leaf, sizeof (leaf), "%s.%s", name, type);
		else
		{
			char guid[37];
			FormatMPRGUID (guid, e->guid);
			snprintf (leaf, sizeof (leaf), "%s.%s", guid, type);
		}
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", leaf);
		if (!valid_sarc_path (leaf))
		{
			char guid[37];
			FormatMPRGUID (guid, e->guid);
			snprintf (path, sizeof (path), "%s/%s%s.%s", dest, basedir ? basedir : "", guid, type);
		}
		File_t F;
		// STRG names may carry folder components and dest itself may not
		// exist yet -- let CreateFile build the path, as the RZ extractor
		// does for the same reason.
		const bool saved_mkdir = opt_mkdir;
		opt_mkdir = true;
		err = CreateFileOpt (&F, true, path, false, arg);
		opt_mkdir = saved_mkdir;
		if (!err && F.f)
		{
			if (data_size && fwrite (data, 1, data_size, F.f) != data_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", data_size, path);
			else if (foot_size && fwrite (foot, 1, foot_size, F.f) != foot_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", foot_size, path);
		}
		ResetFile (&F, opt_preserve);
		FREE (data);
		FREE (foot);
	}

	ResetMPRPACK (&pak);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}
