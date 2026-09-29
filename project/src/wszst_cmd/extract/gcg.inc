// Rainbow Studios .gcg geometry (Disney-Pixar Cars) -> GLB. Materials name a
// .gcm ini file (beside the geometry or in a sibling Tex directory) whose
// TextureMap_1 names the .gct texture; that texture is written as a PNG beside
// the GLB so the exporter can embed it.
typedef struct
{
	char dir[PATH_MAX];
	char png[PATH_MAX];
} rainbow_tex_ctx_t;

// Case-insensitive lookup of NAME+EXT in DIR (Cars mixes cases freely).
static bool rainbow_find (ccp dir, ccp name, ccp ext, char *out, uint out_size)
{
	DIR *d = opendir (dir);
	if (!d)
		return false;
	char want[PATH_MAX];
	snprintf (want, sizeof (want), "%s%s", name, ext);
	bool found = false;
	struct dirent *de;
	while (!found && (de = readdir (d)))
		if (!strcasecmp (de->d_name, want))
		{
			snprintf (out, out_size, "%s/%s", dir, de->d_name);
			found = true;
		}
	closedir (d);
	return found;
}

static ccp rainbow_texname (void *vctx, ccp material)
{
	rainbow_tex_ctx_t *c = vctx;
	static const ccp subdirs[] = { "", "/../Tex", "/../../Tex", "/Tex", "/../../Global/Tex", 0 };
	for (uint i = 0; subdirs[i]; i++)
	{
		char dir[PATH_MAX], path[PATH_MAX], tex[256];
		snprintf (dir, sizeof (dir), "%s%s", c->dir, subdirs[i]);
		snprintf (tex, sizeof (tex), "%s", material);
		if (rainbow_find (dir, material, ".gcm", path, sizeof (path)))
		{
			u8 *ini = 0;
			size_t ini_size = 0;
			if (!LoadFileAlloc (path, 0, 0, &ini, &ini_size, 0, 0, 0, false) && ini)
			{
				char *text = MALLOC (ini_size + 1);
				if (text)
				{
					memcpy (text, ini, ini_size);
					text[ini_size] = 0;
					for (char *line = text; line && *line;)
					{
						char *next = strchr (line, '\n');
						if (next)
							*next++ = 0;
						if (!strncasecmp (line, "TextureMap_1", 12))
						{
							char *eq = strchr (line, '=');
							if (eq)
							{
								eq++;
								while (*eq == ' ')
									eq++;
								size_t l = strlen (eq);
								while (l && (eq[l - 1] == '\r' || eq[l - 1] == ' '))
									eq[--l] = 0;
								if (l && l < sizeof (tex))
									memcpy (tex, eq, l + 1);
							}
							break;
						}
						line = next;
					}
					FREE (text);
				}
			}
			FREE (ini);
		}
		char gct[PATH_MAX];
		if (!rainbow_find (dir, tex, ".gct", gct, sizeof (gct)))
			continue;
		snprintf (c->png, sizeof (c->png), "%s.png", tex);
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s/%s", c->dir, c->png);
		struct stat st;
		if (stat (png, &st))
		{
			u8 *raw = 0;
			size_t raw_size = 0;
			u8 *rgba = 0;
			uint w = 0, h = 0;
			if (!LoadFileAlloc (gct, 0, 0, &raw, &raw_size, 0, 0, 0, false) && raw
				&& raw_size <= UINT_MAX && !DecodeGCT (&rgba, &w, &h, raw, (uint)raw_size))
				SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png, 0, true);
			FREE (raw);
			if (stat (png, &st))
				continue;
		}
		return c->png;
	}
	return 0;
}

static enumError extract_gcg_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".gcg"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX || raw_size < 12 || memcmp (raw, "gcg", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (!IsRainbowGCG (raw, (uint)raw_size))
	{
		// multi-object animation geometry: recognised, not converted
		FREE (raw);
		return ERR_OK;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");
	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT GCG:%s -> GLB:%s\n", arg, dest);
		return ERR_OK;
	}
	if (blitz_output_current (arg, dest))
	{
		FREE (raw);
		return ERR_OK;
	}

	rainbow_tex_ctx_t *ctx = CALLOC (1, sizeof (*ctx));
	if (!ctx)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	StringCopyS (ctx->dir, sizeof (ctx->dir), dest);
	char *slash = strrchr (ctx->dir, '/');
	if (slash)
		*slash = 0;
	else
		StringCopyS (ctx->dir, sizeof (ctx->dir), ".");

	model_t *model = ParseRainbowGCG (raw, (uint)raw_size, rainbow_texname, ctx);
	FREE (raw);
	FREE (ctx);
	if (!model)
		return ERR_OK;
	ExportModelToGLB (model, dest);
	FreeModel (model);

	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT GCG:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return ERR_OK;
}
