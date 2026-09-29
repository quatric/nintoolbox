// Genki "GTI Club: Supermini Festa!" car resource table (.unq.xtd)
static enumError extract_xtd_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".xtd"))
		return ERR_NOTHING_TO_DO;

	return ExtractXTDArchive (arg, basedir, depth);
}
