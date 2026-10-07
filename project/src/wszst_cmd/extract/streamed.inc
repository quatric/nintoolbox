// Shared writer for archives scanned from an open file: each member is
// copied from the source in 1 MiB chunks, so multi-GB archives never have to
// fit in memory. Takes ownership of neither the entries nor the stream.
static enumError extract_streamed_entries (ccp arg, ccp basedir, uint depth, ccp label, FILE *src,
	const stream_entry_t *entries, uint n_entries)
{
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT %s:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", label, arg, n_entries, dest);

	enumError err = ERR_OK;
	u8 *buf = testmode ? 0 : MALLOC (0x100000);
	if (!testmode && !buf)
		return ERR_CANT_CREATE;
	for (uint i = 0; !err && !testmode && i < n_entries; i++)
	{
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
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", chunk, path);
			left -= (u32)chunk;
		}
		ResetFile (&F, opt_preserve);
	}
	FREE (buf);

	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}
