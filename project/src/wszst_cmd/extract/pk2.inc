// Nordcurrent BigFile PK2 multi-part archive (.pk2/.p00...)
static enumError extract_pk2_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pk2"))
		return ERR_NOTHING_TO_DO;

	return ExtractPK2Archive (arg, basedir, depth);
}
