// Decode a Nintendo Binary Font TrueType Font (.bfttf) to a sibling .ttf.
static enumError extract_bfttf_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bfttf"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (raw_size > UINT_MAX || !IsBFTTF (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u8 *ttf = 0;
	uint ttf_size = 0;
	enumError err = DecodeBFTTF (&ttf, &ttf_size, raw, (uint)raw_size);
	FREE (raw);
	if (err)
		return err;

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".ttf");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BFTTF:%s -> TTF:%s (%u bytes)\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, ttf_size);

	if (!testmode)
		err = SaveFile (dest, 0, 0, ttf, ttf_size, 0);

	FREE (ttf);
	return err;
}
