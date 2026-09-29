// Gamebryo .nif (Wii, Pocoyo Racing): embedded GX textures -> PNG beside the
// file, scene geometry -> GLB (world space, bind pose, textured).
static enumError extract_nif_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".nif"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	nif_t *nif = raw_size <= UINT_MAX ? NifOpen (raw, (uint)raw_size) : 0;
	if (!nif)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX], dir[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");
	StringCopyS (dir, sizeof (dir), dest);
	char *slash = strrchr (dir, '/');
	if (slash)
		*slash = 0;
	else
		StringCopyS (dir, sizeof (dir), ".");
	if (testmode)
	{
		fprintf (stdlog, "WOULD EXTRACT NIF:%s -> GLB:%s\n", arg, dest);
		NifClose (nif);
		FREE (raw);
		return ERR_OK;
	}

	const uint nt = NifNumTextures (nif);
	ccp *png = CALLOC (nt ? nt : 1, sizeof (*png));
	char (*png_name)[128] = CALLOC (nt ? nt : 1, sizeof (*png_name));
	if (!png || !png_name)
	{
		FREE (png);
		FREE (png_name);
		NifClose (nif);
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	for (uint i = 0; i < nt; i++)
	{
		char path[PATH_MAX];
		snprintf (png_name[i], sizeof (png_name[i]), "%s.png", NifTextureName (nif, i));
		snprintf (path, sizeof (path), "%s/%s", dir, png_name[i]);
		struct stat st;
		if (stat (path, &st))
		{
			u8 *rgba = 0;
			uint w = 0, h = 0;
			if (!NifDecodeTexture (nif, i, &rgba, &w, &h))
			{
				if (!SaveDecodedRGBAToPNG (rgba, w, h, &be_func, path, 0, true) && verbose >= 0)
					fprintf (
						stdlog, "%sEXTRACT NIF:%s -> PNG:%s\n", verbose > 0 ? "\n" : "", arg, path);
			}
		}
		if (!stat (path, &st))
			png[i] = png_name[i];
	}

	if (!blitz_output_current (arg, dest))
	{
		model_t *model = NifBuildModel (nif, png);
		if (model)
		{
			ExportModelToGLB (model, dest);
			FreeModel (model);
			if (verbose >= 0)
				fprintf (
					stdlog, "%sEXTRACT NIF:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (png);
	FREE (png_name);
	NifClose (nif);
	FREE (raw);
	return ERR_OK;
}
