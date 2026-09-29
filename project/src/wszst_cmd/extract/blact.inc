// Actors name their textures by CRC; resolve them against the .bltex files
// unpacked beside the actor and make sure the sibling PNG exists so the GLB
// exporter can embed it.
typedef struct
{
	char dir[PATH_MAX];
	char png[PATH_MAX];
} blitz_tex_ctx_t;

static ccp blitz_texname (void *vctx, u32 crc)
{
	blitz_tex_ctx_t *c = vctx;
	DIR *dir = opendir (c->dir);
	if (!dir)
		return 0;
	ccp result = 0;
	struct dirent *de;
	while (!result && (de = readdir (dir)))
	{
		const size_t len = strlen (de->d_name);
		if (len <= 6 || len >= sizeof (c->png) - 8 || strcmp (de->d_name + len - 6, ".bltex"))
			continue;
		char base[PATH_MAX];
		memcpy (base, de->d_name, len - 6);
		base[len - 6] = 0;
		if (RevNameCRC (base) != crc)
			continue;
		char src[PATH_MAX];
		snprintf (src, sizeof (src), "%s/%s", c->dir, de->d_name);
		snprintf (c->png, sizeof (c->png), "%s.png", base);
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s/%s", c->dir, c->png);
		struct stat st;
		if (stat (png, &st))
		{
			u8 *raw = 0;
			size_t raw_size = 0;
			if (!LoadFileAlloc (src, 0, 0, &raw, &raw_size, 0, 0, 0, false) && raw
				&& raw_size <= UINT_MAX)
				write_blitz_texture_png (raw, (uint)raw_size, png);
			FREE (raw);
			if (stat (png, &st))
				continue;
		}
		result = c->png;
	}
	closedir (dir);
	return result;
}

static enumError extract_blact_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".blact"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX || !IsBlitzActor (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");
	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT BLACT:%s -> GLB:%s\n", arg, dest);
		return ERR_OK;
	}

	if (blitz_output_current (arg, dest))
	{
		FREE (raw);
		return ERR_OK;
	}

	blitz_tex_ctx_t *ctx = CALLOC (1, sizeof (*ctx));
	if (!ctx)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	StringCopyS (ctx->dir, sizeof (ctx->dir), arg);
	char *slash = strrchr (ctx->dir, '/');
	if (slash)
		*slash = 0;
	else
		StringCopyS (ctx->dir, sizeof (ctx->dir), ".");

	model_t *model = ParseBlitzActor (raw, (uint)raw_size, blitz_texname, ctx);
	FREE (raw);
	FREE (ctx);
	if (!model)
		return ERR_OK; // camera / animation-only actors carry no geometry

	ExportModelToGLB (model, dest);
	FreeModel (model);

	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT BLACT:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return ERR_OK;
}
