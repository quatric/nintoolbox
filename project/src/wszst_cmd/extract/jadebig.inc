// Ubisoft Jade BigFile (".bf", "BIG\0" / "BUG\0"). Streams each member from
// the source so multi-GB archives never have to fit in memory.
static enumError extract_jadebig_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bf"))
		return ERR_NOTHING_TO_DO;

	FILE *src = fopen (arg, "rb");
	if (!src)
		return ERR_NOTHING_TO_DO;
	struct stat st;
	jadebig_entry_t *entries = 0;
	uint n_entries = 0;
	if (fstat (fileno (src), &st) || st.st_size <= 0
		|| ScanJadeBig (src, (u64)st.st_size, &entries, &n_entries) || !n_entries)
	{
		fclose (src);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT JADE-BIG:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_entries, dest);

	enumError err = ERR_OK;
	u8 *buf = testmode ? 0 : MALLOC (0x100000);
	for (uint i = 0; !err && i < n_entries && (testmode || buf); i++)
	{
		if (testmode)
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", entries[i].name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fseeko (src, (off_t)entries[i].offset, SEEK_SET))
			err = FILEERROR1 (&F, ERR_READ_FAILED, "Seek failed: %s\n", arg);
		for (u32 left = entries[i].size; F.f && !err && left;)
		{
			const size_t chunk = left < 0x100000 ? left : 0x100000;
			if (fread (buf, 1, chunk, src) != chunk)
				err = FILEERROR1 (&F, ERR_READ_FAILED, "Reading member failed: %s\n", arg);
			else if (fwrite (buf, 1, chunk, F.f) != chunk)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", chunk, path);
			left -= (u32)chunk;
		}
		ResetFile (&F, opt_preserve);
	}
	if (!testmode && !buf)
		err = ERR_CANT_CREATE;
	FREE (buf);
	FreeJadeBig (entries, n_entries);
	fclose (src);

	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}
