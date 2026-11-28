// Genki "GTI Club: Supermini Festa!" car model container (.mdl)
static enumError extract_xmd_file (ccp arg, ccp basedir, uint depth)
{
	if (!file_has_magic (arg, "XMD\x01", 4))
		return ERR_NOTHING_TO_DO;
	return ExtractXMDArchive (arg, basedir, depth);
}
