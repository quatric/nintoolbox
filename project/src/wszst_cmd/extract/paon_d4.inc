// Paon (Donkey Kong: Barrel Blast) .d4: one GX texture behind a 0x20 byte
// header -> name.png beside it.
static enumError extract_paon_d4_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".d4"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX || !IsPaonD4 (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".png");
	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT PAON-D4:%s -> PNG:%s\n", arg, dest);
		return ERR_OK;
	}
	if (blitz_output_current (arg, dest))
	{
		FREE (raw);
		return ERR_OK;
	}
	err = SavePaonD4PNG (raw, (uint)raw_size, dest);
	FREE (raw);
	if (!err && verbose >= 0)
		fprintf (stdlog, "%sEXTRACT PAON-D4:%s -> PNG:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}
