// Behaviour Interactive "Thor" resource package (.wii; Phineas and Ferb:
// Quest for Cool Stuff, Wii) -- NOT a Wii disc image.
static enumError extract_thor_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".wii"))
		return ERR_NOTHING_TO_DO;

	return ExtractThorPkg (arg, basedir, depth);
}
