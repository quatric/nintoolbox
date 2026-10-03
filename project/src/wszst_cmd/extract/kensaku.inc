// And-Kensaku ".rz": whole-stream Nintendo LZ77 wrapping a "Pres" archive.
// See lib-kensaku.c; ported from Luigi Auriemma's and_kensaku.bms.
static enumError extract_kensaku_members (kensaku_t *kp, ccp arg, ccp basedir, uint depth);

static enumError extract_rz_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".rz"))
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
	if (raw_size < 4 || (raw[0] != 0x10 && raw[0] != 0x11))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	kensaku_t k;
	err = ScanKensakuRZ (&k, raw, raw_size);
	FREE (raw);
	if (err)
		// A non-Pres LZ77 payload is not ours; let the generic Nintendo
		// decompressor further down the chain handle it.
		return ERR_NOTHING_TO_DO;
	return extract_kensaku_members (&k, arg, basedir, depth);
}

// Shared back end for extract_rz_file() and extract_res_file(): 'k' is an
// already-scanned Pres archive; write every member beside the source and
// recurse into the result.
static enumError extract_kensaku_members (kensaku_t *kp, ccp arg, ccp basedir, uint depth)
{
	kensaku_t k = *kp;
	enumError err = ERR_OK;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT RES:%s (%u entries%s) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, k.n_entries,
			k.compression ? (k.compression == 0x11 ? ", LZ11" : ", LZ10") : "", dest);

	for (uint i = 0; i < k.n_entries; i++)
	{
		const kensaku_entry_t *e = k.entries + i;
		char rel[PATH_MAX];
		snprintf (rel, sizeof (rel), "%s", e->name);
		if (!valid_sarc_path (rel))
			snprintf (rel, sizeof (rel), "file_%04u.bin", i);
		if (testmode)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", rel);
		File_t F;
		// Members may carry folder components and the ".../<name>.res.rz.d"
		// root itself may not exist yet -- let CreateFile build the path.
		const bool saved_mkdir = opt_mkdir;
		opt_mkdir = true;
		// Keep going on a single bad member: one unwritable path should not
		// cost every remaining file in the archive.
		enumError ferr = CreateFileOpt (&F, true, path, false, arg);
		opt_mkdir = saved_mkdir;
		if (F.f && fwrite (k.blob + e->offset, 1, e->size, F.f) != e->size)
			ferr
				= FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
		const enumError close_err = ResetFile (&F, opt_preserve);
		if (close_err > ferr)
			ferr = close_err;
		if (ferr > err)
			err = ferr;
		if (ferr == ERR_WRITE_FAILED || close_err)
			break;
	}

	ResetKensaku (&k);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}
