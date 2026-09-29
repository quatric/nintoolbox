// LZ10-compressed RTDP/WTMD wrapper (.cxd; Arc Rise Fantasia, Wii)
static enumError extract_cxd_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".cxd"))
		return ERR_NOTHING_TO_DO;

	return ExtractCXDFile (arg, basedir, depth);
}
