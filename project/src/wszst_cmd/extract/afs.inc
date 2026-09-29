// Sega/CRI AFS archive (.afs; Dragon Ball Z: Budokai Tenkaichi 3, Wii)
static enumError extract_afs_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".afs"))
		return ERR_NOTHING_TO_DO;

	return ExtractAFSArchive (arg, basedir, depth);
}
