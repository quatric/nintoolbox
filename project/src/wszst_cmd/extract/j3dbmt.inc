// J3D material table "BMT" ("J3D2bmt3", GameCube/Wii): the TEX1 section holds the material textures; every texture is written as "<file>.d/<name>.png"
// (plus "<name>_mipN.png" for mipmaps) using the same GX decoder as the J3D models.
static enumError extract_j3dbmt_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bmt") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 0x40 && !memcmp (raw, "J3D2bmt3", 8))
	{
		char dest_dir[PATH_MAX], probe[PATH_MAX];
		beside_source_dest (dest_dir, sizeof (dest_dir), arg);
		if (basedir && *basedir)
			snprintf (dest_dir + strlen (dest_dir), sizeof (dest_dir) - strlen (dest_dir), "/%s", basedir);
		if (!testmode)
			CreatePath (dest_dir, true);
		snprintf (probe, sizeof (probe), "%s/x", dest_dir);
		const int n = testmode ? 0 : ExportJ3DBMTTextures (raw, sz, probe);
		if (n > 0 || testmode)
		{
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT J3D-BMT:%s -> %s (%d textures)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest_dir, n);
			err = ERR_OK;
		}
	}
	FREE (raw);
	return err;
}
