// Extract a TRPAK (.trpak) FlatBuffers archive: a text manifest of every entry (hash,
// compression, sizes), plus its raw data blob per entry. Entries carry no archive-supplied name
// string at all -- only a bare u64 hash meant to be looked up in an external, game-specific name
// database this project doesn't have -- so there is nothing here that needs valid_sarc_path():
// filenames are built purely from the entry index and its hash, never from archive content.
// An OODLE-compressed entry's bytes are written verbatim (still compressed, ".oodle.bin"
// extension) since no Oodle decompressor exists in this codebase, same "don't guess, don't
// mislabel" approach as extract_rpak_file's own decompress_failed fallback.
static enumError extract_trpak_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".trpak"))
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
	if (!IsTRPAK (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	trpak_t trpak;
	err = ScanTRPAK (&trpak, raw, (uint)raw_size);
	if (err)
	{
		FREE (raw);
		return ERROR0 (ERR_INVALID_DATA, "TRPAK: %s: invalid archive\n", arg);
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT TRPAK:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, trpak.n_entries, dest);

	if (!testmode)
	{
		char manifest[PATH_MAX];
		snprintf (manifest, sizeof (manifest), "%s/trpak.txt", dest);
		File_t F;
		err = CreateFileOpt (&F, true, manifest, false, arg);
		if (F.f)
			err = DecodeTRPAK_Text (F.f, &trpak);
		const enumError close_err = ResetFile (&F, opt_preserve);
		if (close_err > err)
			err = close_err;
	}

	for (uint i = 0; !err && i < trpak.n_entries && !testmode; i++)
	{
		const trpak_entry_t *e = trpak.entries + i;
		if ((u64)e->data_offset + e->data_size > raw_size) // ScanTRPAK already guarantees this;
			continue; // kept as defense in depth

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%04u_%016llx.%s", dest, i, (unsigned long long)e->hash,
			e->compression == 3 ? "oodle.bin" : "bin");

		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && e->data_size
			&& fwrite (raw + e->data_offset, 1, e->data_size, F.f) != e->data_size)
			err = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->data_size, path);
		const enumError close_err = ResetFile (&F, opt_preserve);
		if (close_err > err)
			err = close_err;
	}

	ResetTRPAK (&trpak);
	FREE (raw);
	if (!err && !testmode)
	{
		enumError sub_err = extract_tree_complete (dest, depth + 1);
		if (err < sub_err)
			err = sub_err;
	}
	return err;
}
