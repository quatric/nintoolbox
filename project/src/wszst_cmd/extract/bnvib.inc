// Decode a Nintendo Switch Joy-Con Vibration Binary (.bnvib/.nvib) to readable text.
static enumError extract_bnvib_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bnvib") && !is_ext (arg, ".nvib"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (raw_size > UINT_MAX || !IsBNVIB (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	bnvib_t vib;
	enumError err = ScanBNVIB (&vib, raw, (uint)raw_size);
	FREE (raw);
	if (err)
		return err;

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".txt");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BNVIB:%s -> TEXT:%s (%u samples)\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, vib.sample_count);

	if (!testmode)
		err = SaveTextBNVIB (&vib, dest);

	ResetBNVIB (&vib);
	return err;
}
