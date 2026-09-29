// "Bj" engine .mtm mesh tree -> GLB. Textures live in the sibling 8_16/*.tx1
// (+ .tx2); the ones a model uses are written as PNGs beside the GLB.
typedef struct
{
	char dir[PATH_MAX];
	char png[PATH_MAX];
	u8 *t1, *t2;
	size_t s1, s2;
	char base[256];
} bj_tex_ctx_t;

static ccp bj_texname (void *vctx, uint id)
{
	bj_tex_ctx_t *c = vctx;
	if (!c->t1)
		return 0;
	snprintf (c->png, sizeof (c->png), "%s_%u.png", c->base, id);
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/%s", c->dir, c->png);
	struct stat st;
	if (!stat (path, &st))
		return c->png;
	u8 *rgba = 0;
	uint w = 0, h = 0;
	if (DecodeBjTexture (&rgba, &w, &h, c->t1, c->s1, c->t2, c->s2, id)
		|| SaveDecodedRGBAToPNG (rgba, w, h, &be_func, path, 0, true))
		return 0;
	return c->png;
}

static enumError extract_bj_mtm_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".mtm"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (!IsBjMtm (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");
	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT BJ-MESH:%s -> GLB:%s\n", arg, dest);
		return ERR_OK;
	}
	if (blitz_output_current (arg, dest))
	{
		FREE (raw);
		return ERR_OK;
	}
	bj_tex_ctx_t *ctx = CALLOC (1, sizeof (*ctx));
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
	// first *.tx1 in ../8_16 (or the model's own directory)
	static const ccp subdirs[] = { "/../8_16", "", 0 };
	for (uint i = 0; subdirs[i] && !ctx->t1; i++)
	{
		char td[PATH_MAX];
		snprintf (td, sizeof (td), "%s%s", ctx->dir, subdirs[i]);
		DIR *dh = opendir (td);
		struct dirent *de;
		while (dh && !ctx->t1 && (de = readdir (dh)))
		{
			const size_t l = strlen (de->d_name);
			if (l < 5 || strcasecmp (de->d_name + l - 4, ".tx1"))
				continue;
			char p1[PATH_MAX], p2[PATH_MAX];
			snprintf (p1, sizeof (p1), "%s/%s", td, de->d_name);
			snprintf (p2, sizeof (p2), "%s/%.*s.tx2", td, (int)(l - 4), de->d_name);
			if (LoadFileAlloc (p1, 0, 0, &ctx->t1, &ctx->s1, 0, 0, 0, false) || !ctx->t1
				|| LoadFileAlloc (p2, 0, 0, &ctx->t2, &ctx->s2, 0, 0, 0, false) || !ctx->t2)
			{
				FREE (ctx->t1);
				FREE (ctx->t2);
				ctx->t1 = ctx->t2 = 0;
				continue;
			}
			snprintf (ctx->base, sizeof (ctx->base), "%.*s", (int)(l - 4), de->d_name);
		}
		if (dh)
			closedir (dh);
	}
	model_t *model = ParseBjMtm (raw, raw_size, bj_texname, ctx);
	FREE (raw);
	FREE (ctx->t1);
	FREE (ctx->t2);
	FREE (ctx);
	if (!model)
		return ERR_OK;
	ExportModelToGLB (model, dest);
	FreeModel (model);
	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT BJ-MESH:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return ERR_OK;
}
