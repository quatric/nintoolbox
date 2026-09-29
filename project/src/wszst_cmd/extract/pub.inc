// Atomic Planet PUB package (*WII.PUB, Wii): GX textures -> PNG and meshes ->
// GLB (textured by name hash), in a directory named after the package.
typedef struct
{
	const pub_texture_t *list;
	uint count;
	char png[64];
} pub_tex_ctx_t;

static ccp pub_texname (void *vctx, u32 hash)
{
	pub_tex_ctx_t *c = vctx;
	for (uint i = 0; i < c->count; i++)
		if (c->list[i].hash == hash)
		{
			snprintf (c->png, sizeof (c->png), "%04u_%08x.png", i, hash);
			return c->png;
		}
	return 0;
}

static enumError extract_pub_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pub"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	uint count = 0, mcount = 0;
	pub_texture_t *list = 0;
	pub_object_t *meshes = 0;
	if (IsAtomicPub (raw, raw_size))
	{
		list = ListPubTextures (raw, raw_size, &count);
		meshes = ListPubMeshes (raw, raw_size, &mcount);
	}
	if ((!list || !count) && (!meshes || !mcount))
	{
		FREE (list);
		FREE (meshes);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PUB:%s (%u textures, %u meshes) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, count, mcount, dest);

	enumError err = ERR_OK;
	for (uint i = 0; i < count && !err && !testmode; i++)
	{
		u8 *rgba = 0;
		if (DecodePubTexture (&rgba, list + i))
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%04u_%08x.png", dest, basedir ? basedir : "", i,
			list[i].hash);
		err = SaveDecodedRGBAToPNG (rgba, list[i].width, list[i].height, &be_func, path, 0, true);
	}
	pub_tex_ctx_t tctx = { list, count, "" };
	for (uint i = 0; i < mcount && !err && !testmode; i++)
	{
		model_t *model = ParsePubMesh (raw + meshes[i].offset, meshes[i].size, pub_texname, &tctx);
		if (!model)
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%smesh_%04u_%08x.glb", dest, basedir ? basedir : "", i,
			meshes[i].hash);
		ExportModelToGLB (model, path);
		FreeModel (model);
	}
	FREE (list);
	FREE (meshes);
	FREE (raw);
	return err;
}
