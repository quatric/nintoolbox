// Telltale Tool legacy archive (.ttarch; Back to the Future: The Game, Wii)
static enumError extract_ttarch_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".ttarch"))
		return ERR_NOTHING_TO_DO;

	return ExtractTtarch (arg, basedir, depth);
}
