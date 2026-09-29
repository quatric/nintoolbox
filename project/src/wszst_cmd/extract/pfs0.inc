// Extract a Nintendo Switch Partition File System (.nsp / .pfs0 / .hfs0) natively.
static enumError extract_pfs0_file (ccp arg, ccp basedir, uint depth)
{
	const bool is_nsp = is_ext (arg, ".nsp");
	const bool is_pfs = is_ext (arg, ".pfs0") || is_ext (arg, ".hfs0");
	if (!is_nsp && !is_pfs)
		return ERR_NOTHING_TO_DO;

	FILE *fp = fopen (arg, "rb");
	if (!fp)
		return ERR_NOTHING_TO_DO;
	char magic[4];
	size_t read_bytes = fread (magic, 1, 4, fp);
	fclose (fp);
	if (read_bytes != 4 || (memcmp (magic, "PFS0", 4) && memcmp (magic, "HFS0", 4)))
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);

	enumError err = ExtractPFS0Archive (arg, basedir, depth);
	if (err == ERR_OK && !testmode)
		return extract_tree_complete (dest, depth + 1);

	return err;
}
