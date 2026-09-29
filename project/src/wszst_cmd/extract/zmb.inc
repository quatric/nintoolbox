static enumError extract_zmb_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err || raw_size < 0x24 || raw_size > UINT_MAX || !IsZMB (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".zmb.yaml");

	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT ZMB:%s -> YAML:%s\n", arg, dest);
		return ERR_OK;
	}

	err = DecodeZMB (raw, (uint)raw_size, dest);
	FREE (raw);
	return err;
}
