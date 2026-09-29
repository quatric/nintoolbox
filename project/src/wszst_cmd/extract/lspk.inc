// Extract Mistwalker's "foo.pk"/"foo.pkh" archive pair (The Last Story).
// Triggers on the ".pk" file; the sibling ".pkh" table (same basename, next
// to it) must exist or this isn't a real LSPK pair. A third sibling,
// ".pfs", carries filenames but is deliberately not read -- see the format
// comment on ScanLSPK() in lib-nintendo.h.
static enumError extract_lspk_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pk"))
		return ERR_NOTHING_TO_DO;

	// Sibling-*input* lookup: must stay next to 'arg' on disk regardless of
	// --dest (which only ever renames the *output*) -- beside_source_dest()
	// and beside_source_dest_ext() are the wrong tool here since they defer
	// to opt_dest.
	char pkh_path[PATH_MAX];
	{
		char base[PATH_MAX];
		const uint len = snprintf (base, sizeof (base), "%s", arg);
		if (len >= sizeof (base))
			return ERR_NOTHING_TO_DO;
		char *dot = strrchr (base, '.');
		char *slash = strrchr (base, '/');
		if (dot && (!slash || dot > slash))
			*dot = 0;
		snprintf (pkh_path, sizeof (pkh_path), "%s.pkh", base);
	}
	struct stat st;
	if (stat (pkh_path, &st) != 0 || !S_ISREG (st.st_mode))
		return ERR_NOTHING_TO_DO;

	u8 *pk_raw = 0;
	size_t pk_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &pk_raw, &pk_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (pk_size > UINT_MAX)
	{
		FREE (pk_raw);
		return ERR_FILE_TOO_BIG;
	}

	u8 *pkh_raw = 0;
	size_t pkh_size = 0;
	err = LoadFileAlloc (pkh_path, 0, 0, &pkh_raw, &pkh_size, 0, 0, 0, false);
	if (err)
	{
		FREE (pk_raw);
		return ERR_NOTHING_TO_DO;
	}
	if (pkh_size > UINT_MAX)
	{
		FREE (pk_raw);
		FREE (pkh_raw);
		return ERR_FILE_TOO_BIG;
	}

	lspk_t pak;
	err = ScanLSPK (&pak, pkh_raw, pkh_size, pk_raw, pk_size);
	if (err)
	{
		FREE (pk_raw);
		FREE (pkh_raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT LSPK:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, pak.n_entries, dest);

	for (uint i = 0; !err && i < pak.n_entries; i++)
	{
		const lspk_entry_t *e = pak.entries + i;
		if (testmode)
			continue;

		u8 *dec = 0;
		uint dec_size = 0;
		const u8 *out_data = e->data;
		uint out_size = e->size;

		// Compression is per-archive in the real format, but detecting it
		// per-entry from the payload's own magic byte is equivalent and
		// avoids a separate whole-archive pre-scan: 0x10/0x11 is Nintendo
		// LZ10/LZ11 (this codebase's own decoder), 0x78 is a plain zlib
		// stream, anything else (including dec_size==0, i.e. "not
		// compressed" per the .pkh table) is stored as-is.
		if (e->dec_size && e->size >= 1)
		{
			if ((e->data[0] == 0x10 || e->data[0] == 0x11)
				&& DecodeLZ10LZ11 (&dec, &dec_size, e->data, e->size) == ERR_OK)
			{
				out_data = dec;
				out_size = dec_size;
			}
			else if (e->data[0] == 0x78)
			{
				u8 *zdec = 0;
				uint zdec_size = 0;
				if (DecodeZlibGrow (&zdec, &zdec_size, e->data, e->size) == ERR_OK)
				{
					dec = zdec;
					out_data = dec;
					out_size = zdec_size;
				}
			}
		}

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%08x.bin", dest, basedir ? basedir : "", e->hash);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && out_size && fwrite (out_data, 1, out_size, F.f) != out_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", out_size, path);
		ResetFile (&F, opt_preserve);
		FREE (dec);
	}

	ResetLSPK (&pak);
	FREE (pk_raw);
	FREE (pkh_raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}
