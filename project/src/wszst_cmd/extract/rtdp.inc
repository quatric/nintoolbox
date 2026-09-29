// imageEpoch RTDP flat archive (.vol; Arc Rise Fantasia, Wii)
static enumError extract_rtdp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".vol"))
		return ERR_NOTHING_TO_DO;

	return ExtractRTDPArchive (arg, basedir, depth);
}
