// Extract a Nintendo Switch Game Cartridge Image (.xci / HEAD).
static enumError extract_xci_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".xci"))
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);

	enumError err = ExtractXCIArchive (arg, basedir, depth);
	if (err == ERR_OK && !testmode)
		return extract_tree_complete (dest, depth + 1);

	return err;
}
