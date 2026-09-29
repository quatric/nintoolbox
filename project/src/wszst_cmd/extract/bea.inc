// Writes the sidecar CreateBEA() needs to rebuild a byte-faithful archive:
// header fields ScanBEA() can't re-derive (version, byte order, alignment,
// name, ...) plus per-member metadata not implied by the member's own file
// content (unk1/unk2 "type tag", FileID1/2, FileHash/Unknown3, FileType,
// whether it was stored compressed). Plain tab-separated text, one line per
// field/member -- member names never contain a tab.
static void write_bea_setup (ccp dest, const bea_archive_t *bea)
{
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/%s", dest, BEA_SETUP_FILE);
	FILE *f = fopen (path, "wb");
	if (!f)
		return;

	fprintf (f, "BEA-SETUP 1\n");
	fprintf (f, "version\t%u\t%u\t%u\t%u\n", bea->version_major, bea->version_major2,
		bea->version_minor, bea->version_minor2);
	fprintf (f, "byte_order\t%u\n", bea->byte_order);
	fprintf (f, "alignment\t%u\n", bea->alignment);
	fprintf (f, "target_address_size\t%u\n", bea->target_address_size);
	if (bea->name)
		fprintf (f, "name\t%s\n", bea->name);
	if (bea->compression_name)
		fprintf (f, "compression_name\t%s\n", bea->compression_name);
	for (uint i = 0; i < bea->n_references; i++)
		if (bea->reference_list[i])
			fprintf (f, "reference\t%s\n", bea->reference_list[i]);
	for (uint i = 0; i < bea->n_files; i++)
	{
		const bea_file_t *bf = &bea->files[i];
		fprintf (f, "file\t%s\t%u\t%u\t%u\t%llx\t%llx\t%s\t%u\n", bf->name, bf->unk1, bf->unk2,
			bf->unknown3, (unsigned long long)bf->file_id1, (unsigned long long)bf->file_id2,
			*bf->file_type ? bf->file_type : "-", bf->is_compressed ? 1 : 0);
	}

	// The original "_DIC" Patricia trie, node-for-node: if a later CREATE
	// finds this directory still has exactly this set of member names, it
	// reuses these reference/idx_left/idx_right values verbatim instead of
	// re-deriving the trie (see CreateBEA()'s reuse_dict path in
	// lib-bea.c) -- that path is validated against a real sample, the
	// from-scratch trie builder much less so.
	for (uint i = 0; i < bea->n_dict_nodes; i++)
		fprintf (f, "dict\t%u\t%u\t%u\t%s\n", bea->dict[i].reference, bea->dict[i].idx_left,
			bea->dict[i].idx_right, bea->dict[i].key);
	fclose (f);
}

static enumError extract_bea_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err || raw_size < 44 || raw_size > UINT_MAX || !IsBEA (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	bea_archive_t bea;
	err = ScanBEA (&bea, raw, (uint)raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BEA:%s -> %s/ (%u files)\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest, bea.n_files);

	if (!testmode)
	{
		for (uint i = 0; i < bea.n_files; i++)
		{
			// bea.files[i].name is read straight out of the archive's own
			// string table by ScanBEA()/load_bea_string() -- a crafted
			// "../../../tmp/evil" entry would otherwise escape 'dest'.
			ccp name = bea.files[i].name;
			char safe_name[32];
			if (!valid_sarc_path (name))
			{
				snprintf (safe_name, sizeof (safe_name), "%04u.bin", i);
				name = safe_name;
			}
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
			File_t F;
			CreateFileOpt (&F, true, path, false, arg);
			if (F.f && bea.files[i].size)
				fwrite (bea.files[i].data, 1, bea.files[i].size, F.f);
			ResetFile (&F, opt_preserve);
		}
		write_bea_setup (dest, &bea);
	}

	ResetBEA (&bea);
	FREE (raw);

	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}
