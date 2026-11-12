// Kirby Air Riders "phx" asset catalogs: Asset/assets.cat ("PARC") over the
// "PDAT" packs beside it. Each asset lands in "<type>/<id>.<type>[.<block>]".
static enumError extract_parc_file (ccp arg, ccp basedir, uint depth)
{
	u8 head[0x40];
	FILE *fp = fopen (arg, "rb");
	if (!fp)
		return ERR_NOTHING_TO_DO;
	const bool ok = fread (head, 1, sizeof (head), fp) == sizeof (head) && IsPARC (head, sizeof (head));
	fclose (fp);
	if (!ok)
		return ERR_NOTHING_TO_DO;

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	const enumError err = ExtractPARCArchive (arg, basedir, depth);
	if (err <= ERR_WARNING && !testmode)
	{
		const enumError terr = extract_tree_complete (dest, depth + 1);
		return terr > err ? terr : err;
	}
	return err;
}

// A PDAT pack is read through its catalog; on its own it is opaque.
static enumError extract_pdat_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	u8 head[8];
	FILE *fp = fopen (arg, "rb");
	if (!fp)
		return ERR_NOTHING_TO_DO;
	const bool ok = fread (head, 1, sizeof (head), fp) == sizeof (head) && !memcmp (head, "PDAT", 4);
	fclose (fp);
	return ok ? ERR_OK : ERR_NOTHING_TO_DO;
}

// PTEX texture ("<id>.ptex" HEAD flatbuffer + "<id>.ptex.body") -> PNG.
static enumError extract_phx_ptex_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".ptex"))
		return ERR_NOTHING_TO_DO;
	u8 *head = 0, *body = 0;
	size_t head_size = 0, body_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &head, &head_size, 1u << 20, 2, 0, false))
		return ERR_NOTHING_TO_DO;
	if (!IsPTEX (head, (uint)head_size))
	{
		FREE (head);
		return ERR_NOTHING_TO_DO;
	}
	char body_path[PATH_MAX], png[PATH_MAX];
	snprintf (body_path, sizeof (body_path), "%s.body", arg);
	if (LoadFileAlloc (body_path, 0, 0, &body, &body_size, 1u << 30, 2, 0, false))
	{
		FREE (head);
		return ERR_OK; // texture header without its pixels: nothing to decode
	}
	beside_source_dest_ext (png, sizeof (png), arg, ".png");

	u8 *rgba = 0;
	uint w = 0, h = 0;
	ccp fmt = 0;
	enumError err = DecodePTEX_RGBA (&rgba, &w, &h, head, (uint)head_size, body, (uint)body_size, &fmt);
	FREE (head);
	FREE (body);
	if (err)
		return depth > 0 ? ERR_WARNING : err;
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE PTEX:%s (%ux%u %s) -> PNG:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, w, h, fmt ? fmt : "?", png);
	if (testmode)
		FREE (rgba);
	else
		err = SaveDecodedRGBAToPNG (rgba, w, h, &le_func, png, 0, false); // takes ownership
	return err;
}

// phx PMSH mesh: "<id>.pmsh" (Main flatbuffer) + "<id>.pmsh.gbuf" -> "<id>.glb"
static enumError extract_phx_pmsh_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".pmsh"))
		return ERR_NOTHING_TO_DO;
	u8 *main_d = 0, *gbuf = 0;
	size_t main_n = 0, gbuf_n = 0;
	if (LoadFileAlloc (arg, 0, 0, &main_d, &main_n, 16u << 20, 2, 0, false))
		return ERR_NOTHING_TO_DO;
	if (!IsPMSH (main_d, (uint)main_n))
	{
		FREE (main_d);
		return ERR_NOTHING_TO_DO;
	}
	char gpath[PATH_MAX], glb[PATH_MAX];
	snprintf (gpath, sizeof (gpath), "%s.gbuf", arg);
	if (LoadFileAlloc (gpath, 0, 0, &gbuf, &gbuf_n, 1u << 30, 2, 0, false))
	{
		FREE (main_d);
		return ERR_OK; // mesh description without its geometry
	}
	model_t *model = DecodePMSH (main_d, (uint)main_n, gbuf, (uint)gbuf_n);
	FREE (main_d);
	FREE (gbuf);
	if (!model)
		return depth > 0 ? ERR_WARNING : ERROR0 (ERR_INVALID_DATA, "PMSH: no geometry decoded: %s\n", arg);
	beside_source_dest_ext (glb, sizeof (glb), arg, ".glb");
	size_t tris = 0;
	for (size_t i = 0; i < model->num_meshes; i++)
		tris += model->meshes[i].num_vertices / 3;
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE PMSH:%s (%zu submeshes, %zu triangles) -> GLB:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, model->num_meshes, tris, glb);
	if (!testmode)
		ExportModelToGLB (model, glb);
	FreeModel (model);
	return ERR_OK;
}
