// Extract a Monster Games "3LDN"/"2LDN" .mod model to a sibling GLB model file
// Next Level Games .glg/.rlg models (Super Mario Strikers, Mario Strikers
// Charged). These were reachable only through wmdlt: a whole-disc `wszst xx`
// extracted all 149 of Super Mario Strikers' models and converted none of
// them. Textures come from the sibling .glt/.rlt PTLG container, which is a
// plain disc file rather than something this pass produces, so unlike .mod
// Koei Tecmo G1M models (Hyrule Warriors, Fire Emblem Warriors). These sit
// inside LINKDATA members rather than as standalone files on the cart, so
// the extension gate is deliberately loose and the magic decides.
static enumError extract_g1m_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".g1m") && !is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (raw_size < 0x18 || raw_size > UINT_MAX || memcmp (raw, "_M1G", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");

	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT G1M:%s -> GLB:%s\n", arg, dest);
		return ERR_OK;
	}

	const enumError err = DecodeG1M (raw, (uint)raw_size, dest);
	FREE (raw);
	if (err)
		return err;

	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT G1M:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}
