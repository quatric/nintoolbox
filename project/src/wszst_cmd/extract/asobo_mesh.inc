// Asobo Mesh_Z (Ratatouille .DRV member, Wii GX display lists) -> GLB. The
// materials' first texture is resolved through the sibling Material_Z and
// Bitmap_Z members of the same volume and written as a PNG beside the GLB.
typedef struct
{
	char dir[PATH_MAX];
	char png[32];
} asobo_tex_ctx_t;

static ccp asobo_texname (void *vctx, u32 material)
{
	asobo_tex_ctx_t *c = vctx;
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/%08x.Material_Z", c->dir, material);
	u8 *mat = 0;
	size_t mat_size = 0;
	if (LoadFileAlloc (path, 0, 0, &mat, &mat_size, 0, 0, 0, false) || !mat)
		return 0;
	const u32 tex = AsoboMaterialTexture (mat, mat_size);
	FREE (mat);
	if (!tex)
		return 0;
	snprintf (c->png, sizeof (c->png), "%08x.png", tex);
	snprintf (path, sizeof (path), "%s/%s", c->dir, c->png);
	struct stat st;
	if (!stat (path, &st))
		return c->png;
	char src[PATH_MAX];
	snprintf (src, sizeof (src), "%s/%08x.Bitmap_Z", c->dir, tex);
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (src, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
		return 0;
	u8 *rgba = 0;
	uint w = 0, h = 0;
	const enumError err = DecodeAsoboBitmap (&rgba, &w, &h, raw, raw_size);
	FREE (raw);
	if (err || SaveDecodedRGBAToPNG (rgba, w, h, &be_func, path, 0, true))
		return 0;
	return c->png;
}

static enumError extract_asobo_mesh_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".mesh_z"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (!IsAsoboMesh (raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");
	if (testmode)
	{
		FREE (raw);
		fprintf (stdlog, "WOULD EXTRACT ASOBO-MESH:%s -> GLB:%s\n", arg, dest);
		return ERR_OK;
	}
	if (blitz_output_current (arg, dest))
	{
		FREE (raw);
		return ERR_OK;
	}
	asobo_tex_ctx_t *ctx = CALLOC (1, sizeof (*ctx));
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
	model_t *model = ParseAsoboMesh (raw, raw_size, asobo_texname, ctx);
	FREE (raw);
	FREE (ctx);
	if (!model)
		return ERR_OK;
	ExportModelToGLB (model, dest);
	FreeModel (model);
	if (verbose >= 0)
		fprintf (stdlog, "%sEXTRACT ASOBO-MESH:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return ERR_OK;
}
