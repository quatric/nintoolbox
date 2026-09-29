// Extract a Nintendo Switch Executable (.nso / NSO0).
static enumError extract_nso_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".nso"))
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);

	enumError err = ExtractNSOArchive (arg, basedir, depth);
	if (err == ERR_OK && !testmode)
		return extract_tree_complete (dest, depth + 1);

	return err;
}
