// Traveller's Tales FPK resource package (.fpk; Bionicle Heroes, Wii)
static enumError extract_fpk_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".fpk"))
		return ERR_NOTHING_TO_DO;

	return ExtractFPKArchive (arg, basedir, depth);
}
