// Extract a Paper Mario .csb collision scene to a sibling GLB model file.
// .csb has no magic, so this is gated on the extension (including the
// retail Zstandard forms, decompressed first) and DecodeCSB() validates
// the layout structurally. The companion .ctb search table is regenerated
// on repack, so it is not extracted.
static enumError extract_csb_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".csb") && !is_ext (arg, ".csb.zst") && !is_ext (arg, ".csb.zs")
		&& !is_ext (arg, ".csb.zstd"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// retail Switch storage is Zstandard-compressed
	const u8 *data = raw;
	uint data_size = (uint)raw_size;
	u8 *decomp = 0;
	uint decomp_size = 0;
	if (IsZSTD (raw, (uint)raw_size) > 0
		&& DecodeZSTD (&decomp, &decomp_size, raw, (uint)raw_size) == ERR_OK && decomp_size)
	{
		data = decomp;
		data_size = decomp_size;
	}
	if (!IsCSB (data, data_size))
	{
		FREE (decomp);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CSB:%s -> GLB:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);

	if (!testmode)
	{
		err = DecodeCSB (data, data_size, dest);
		// see StampFileMtime()'s doc comment at extract_msh_file()
		if (!err)
			StampFileMtime (dest, arg);
	}
	FREE (decomp);
	FREE (raw);
	return err;
}
