// Extract an NCCARC container (WarioWare: Touched!). Gated on both the
// ".nccarc"/".nccarc_c.bin" extension AND ScanNCCARC's structural checks --
// there's no magic to key off (see the long comment at ScanNCCARC's
// definition), so extension-gating keeps this from firing on unrelated
// files that might coincidentally look like a monotonic offset table.
// Decodes an N64 Virtual Console "romc"-compressed ROM to a plain .z64
// sibling file. Gated on the literal bare filename "romc" -- that's the
// real filename found inside a real retail Kirby 64 (USA) VC WAD's emulator
// app content (no extension, no magic to key off; see the long comment
// above DecodeRomC() in lib-vc.c for the full verification detail). A
// same-generation Yoshi's Story (USA) VC WAD stores its ROM under the name
// "rom" with NO compression at all, so that name is passed through
// untouched by this handler -- decompressing an already-raw ROM would be
// wrong, and there's nothing here to decode it as anyway (real N64 header
static enumError extract_ccf_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".ccf"))
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

	ccf_t ccf;
	err = ScanCCF (&ccf, raw, (uint)raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CCF:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, ccf.n_entries, dest);

	for (uint i = 0; !err && i < ccf.n_entries; i++)
	{
		const ccf_entry_t *e = ccf.entries + i;
		if (testmode)
			continue;

		u8 *decomp = 0;
		uint decomp_size = 0;
		err = DecodeCCFEntry (&decomp, &decomp_size, e);
		if (err)
			continue;

		ccp ename = e->name;
		char fallback[32];
		if (!valid_sarc_path (ename))
		{
			snprintf (fallback, sizeof (fallback), "file_%04u.bin", i);
			ename = fallback;
		}

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", ename);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && decomp_size && fwrite (decomp, 1, decomp_size, F.f) != decomp_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", decomp_size, path);
		ResetFile (&F, opt_preserve);
		FREE (decomp);
	}

	ResetCCF (&ccf);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}
