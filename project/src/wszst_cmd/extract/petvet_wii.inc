// Pet Vet ".wii" textures (see DecodePetVetWiiTexture in lib-granny.c; mip levels are the ".wii1" ... siblings and are
// left alone) -> ".png" beside the source.
static enumError extract_petvet_wii_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".wii"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (!IsPetVetWiiTexture (raw, sz))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".png");
	enumError err = ERR_OK;
	if (testmode)
		fprintf (stdlog, "WOULD DECODE TEXTURE:%s -> PNG:%s\n", arg, dest);
	else
	{
		err = DecodePetVetWiiTexture (raw, (uint)sz, dest);
		if (verbose >= 0 && !err)
			fprintf (stdlog, "%sDECODE TEXTURE:%s -> PNG:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	}
	FREE (raw);
	return err;
}
