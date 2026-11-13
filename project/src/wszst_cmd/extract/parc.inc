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
	beside_source_dest_ext (png, sizeof (png), arg, ".png");
	struct stat st_src, st_png;
	if (!opt_overwrite && !stat (arg, &st_src) && !stat (png, &st_png) && st_png.st_mtime >= st_src.st_mtime)
	{
		FREE (head);
		return ERR_OK; // already decoded (e.g. on demand for a PMSH material)
	}
	if (LoadFileAlloc (body_path, 0, 0, &body, &body_size, 1u << 30, 2, 0, false))
	{
		FREE (head);
		return ERR_OK; // texture header without its pixels: nothing to decode
	}

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

// "<catalog>.d/materials.tsv" (written by ExtractPARCArchive): slot hash -> base-colour candidates.
static struct
{
	char path[PATH_MAX];
	u64 *rows; // {slot, first candidate, candidate count} sorted by slot
	uint n, cap;
	u64 *cand;
	uint n_cand, cap_cand;
	u64 *verdict; // {texture, 1 usable / 2 rejected}, unsorted
	uint n_verdict, cap_verdict;
} pmsh_tsv;

static bool pmsh_push (u64 **a, uint *n, uint *cap, const u64 *v, uint k)
{
	if (*n + k > *cap)
	{
		uint c = *cap ? *cap * 2 : 1024;
		while (c < *n + k)
			c *= 2;
		u64 *na = REALLOC (*a, (size_t)c * sizeof (u64));
		if (!na)
			return false;
		*a = na;
		*cap = c;
	}
	memcpy (*a + *n, v, k * sizeof (u64));
	*n += k;
	return true;
}

static void pmsh_tsv_load (ccp tsv)
{
	if (!strcmp (pmsh_tsv.path, tsv))
		return;
	FREE (pmsh_tsv.rows);
	FREE (pmsh_tsv.cand);
	FREE (pmsh_tsv.verdict);
	memset (&pmsh_tsv, 0, sizeof (pmsh_tsv));
	snprintf (pmsh_tsv.path, sizeof (pmsh_tsv.path), "%s", tsv);
	FILE *f = fopen (tsv, "r");
	char line[4096];
	while (f && fgets (line, sizeof (line), f))
	{
		unsigned long long sl, mt;
		int pos = 0;
		if (line[0] == '#' || sscanf (line, "%16llx\t%16llx\t%n", &sl, &mt, &pos) != 2 || !pos)
			continue;
		u64 row[3] = { sl, pmsh_tsv.n_cand, 0 };
		for (ccp p = line + pos; *p && *p != '\n';)
		{
			char *end;
			const unsigned long long t = strtoull (p, &end, 16);
			if (end == p)
				break;
			pmsh_push (&pmsh_tsv.cand, &pmsh_tsv.n_cand, &pmsh_tsv.cap_cand, (const u64 *)&t, 1);
			row[2]++;
			p = *end == ',' ? end + 1 : end;
		}
		pmsh_push (&pmsh_tsv.rows, &pmsh_tsv.n, &pmsh_tsv.cap, row, 3);
	}
	if (f)
		fclose (f);
}

static const u64 *pmsh_slot_row (u64 slot)
{
	uint lo = 0, hi = pmsh_tsv.n / 3;
	while (lo < hi)
	{
		const uint mid = (lo + hi) / 2;
		const u64 v = pmsh_tsv.rows[3 * mid];
		if (v == slot)
			return pmsh_tsv.rows + 3 * mid;
		if (v < slot)
			lo = mid + 1;
		else
			hi = mid;
	}
	return 0;
}

// A plausible colour map: some opaque texels and not a tangent-space normal map.
static bool pmsh_colour_map (ccp ptex)
{
	u8 *head = 0, *body = 0;
	size_t hn = 0, bn = 0;
	char bp[PATH_MAX];
	snprintf (bp, sizeof (bp), "%s.body", ptex);
	bool ok = false;
	if (!LoadFileAlloc (ptex, 0, 0, &head, &hn, 1u << 20, 2, 0, false)
		&& !LoadFileAlloc (bp, 0, 0, &body, &bn, 1u << 30, 2, 0, false))
	{
		u8 *rgba = 0;
		uint w = 0, h = 0;
		const int sv = verbose;
		verbose = -1;
		if (!DecodePTEX_RGBA (&rgba, &w, &h, head, (uint)hn, body, (uint)bn, 0) && rgba)
		{
			const size_t n = (size_t)w * h;
			const size_t step = n > 65536 ? n / 65536 : 1;
			u64 sum[3] = { 0, 0, 0 }, cnt = 0;
			uint amax = 0;
			for (size_t i = 0; i < n; i += step)
			{
				const u8 *p = rgba + 4 * i;
				sum[0] += p[0], sum[1] += p[1], sum[2] += p[2], cnt++;
				if (p[3] > amax)
					amax = p[3];
			}
			const double r = (double)sum[0] / cnt, g = (double)sum[1] / cnt, bl = (double)sum[2] / cnt;
			const bool normal_map = r > 100 && r < 156 && g > 100 && g < 156 && bl > 200;
			ok = amax > 0 && !normal_map;
		}
		verbose = sv;
		FREE (rgba);
	}
	FREE (head);
	FREE (body);
	return ok;
}

static enumError extract_phx_ptex_file (ccp arg, ccp basedir, uint depth);

// Give each "material_<slot hash>" its base-colour texture "../ptex/<id>.png", decoding it first
// when the tree walk has not reached it yet.
static void pmsh_bind_textures (model_t *model, ccp arg, uint depth)
{
	char dir[PATH_MAX], tsv[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s", arg);
	char *sl = strrchr (dir, '/');
	if (sl)
		*sl = 0;
	else
		snprintf (dir, sizeof (dir), ".");
	snprintf (tsv, sizeof (tsv), "%s/../materials.tsv", dir);
	struct stat st;
	if (stat (tsv, &st))
		return;
	pmsh_tsv_load (tsv);
	for (size_t i = 0; i < model->num_materials; i++)
	{
		material_t *mt = model->materials + i;
		unsigned long long slot;
		if (sscanf (mt->name, "material_%16llx", &slot) != 1 || strlen (mt->name) != 25)
			continue;
		const u64 *row = pmsh_slot_row (slot);
		for (u64 k = 0; row && k < row[2]; k++)
		{
			const u64 tex = pmsh_tsv.cand[row[1] + k];
			char ptex[PATH_MAX], png[PATH_MAX];
			snprintf (ptex, sizeof (ptex), "%s/../ptex/%016llx.ptex", dir, (unsigned long long)tex);
			snprintf (png, sizeof (png), "%s/../ptex/%016llx.png", dir, (unsigned long long)tex);
			u64 verdict = 0;
			for (uint v = 0; v < pmsh_tsv.n_verdict && !verdict; v += 2)
				if (pmsh_tsv.verdict[v] == tex)
					verdict = pmsh_tsv.verdict[v + 1];
			if (!verdict)
			{
				verdict = pmsh_colour_map (ptex) ? 1 : 2;
				const u64 vv[2] = { tex, verdict };
				pmsh_push (&pmsh_tsv.verdict, &pmsh_tsv.n_verdict, &pmsh_tsv.cap_verdict, vv, 2);
			}
			if (verdict != 1)
				continue;
			if (stat (png, &st))
				extract_phx_ptex_file (ptex, 0, depth + 1);
			if (stat (png, &st))
				continue;
			snprintf (mt->textures[0], sizeof (mt->textures[0]), "../ptex/%016llx", (unsigned long long)tex);
			mt->num_textures = 1;
			mt->wrap_s[0] = mt->wrap_t[0] = 1;
			mt->min_filter[0] = mt->mag_filter[0] = 1;
			break;
		}
	}
	// Vertex colour (attribute 24) is often a layer-blend mask (terrain); glTF multiplies it into the
	// base colour, so keep it only on submeshes that did not get a texture.
	for (size_t i = 0; i < model->num_meshes; i++)
	{
		mesh_t *mesh = model->meshes + i;
		const int m = mesh->material_idx;
		if (m < 0 || (size_t)m >= model->num_materials || !model->materials[m].num_textures || !mesh->colors[0])
			continue;
		FREE (mesh->colors[0]);
		mesh->colors[0] = 0;
		mesh->num_colors[0] = 0;
		for (size_t v = 0; v < mesh->num_vertices; v++)
			mesh->vertices[v].color_idx[0] = -1;
	}
}

// "<dest>/skeletons.tsv": {joint name hash, PSKS id} rows sorted by joint, then skeleton.
static struct
{
	char path[PATH_MAX];
	u64 *rows;
	uint n, cap;
} pmsh_skel;

static bool pmsh_skel_has (u64 joint, u64 skel)
{
	uint lo = 0, hi = pmsh_skel.n / 2;
	while (lo < hi)
	{
		const uint mid = (lo + hi) / 2;
		const u64 *r = pmsh_skel.rows + 2 * mid;
		if (r[0] == joint && r[1] == skel)
			return true;
		if (r[0] < joint || (r[0] == joint && r[1] < skel))
			lo = mid + 1;
		else
			hi = mid;
	}
	return false;
}

static void pmsh_skel_load (ccp dir)
{
	char tsv[PATH_MAX];
	snprintf (tsv, sizeof (tsv), "%s/../skeletons.tsv", dir);
	if (!strcmp (pmsh_skel.path, tsv))
		return;
	FREE (pmsh_skel.rows);
	memset (&pmsh_skel, 0, sizeof (pmsh_skel));
	snprintf (pmsh_skel.path, sizeof (pmsh_skel.path), "%s", tsv);
	FILE *f = fopen (tsv, "r");
	char line[128];
	while (f && fgets (line, sizeof (line), f))
	{
		unsigned long long r[2];
		if (line[0] != '#' && sscanf (line, "%16llx\t%16llx", r, r + 1) == 2)
			pmsh_push (&pmsh_skel.rows, &pmsh_skel.n, &pmsh_skel.cap, (const u64 *)r, 2);
	}
	if (f)
		fclose (f);
}

// Replace a skinned mesh's flat palette skeleton with the PSKS that defines all of its joints.
static void pmsh_bind_skeleton (model_t *model, ccp dir)
{
	if (!model->num_joints)
		return;
	pmsh_skel_load (dir);
	unsigned long long j0;
	if (!pmsh_skel.n || sscanf (model->joints[0].name, "joint_%16llx", &j0) != 1)
		return;
	// candidates: every skeleton with the first palette joint; take the first that has them all
	uint lo = 0, hi = pmsh_skel.n / 2;
	while (lo < hi)
	{
		const uint mid = (lo + hi) / 2;
		if (pmsh_skel.rows[2 * mid] < j0)
			lo = mid + 1;
		else
			hi = mid;
	}
	for (uint i = lo; i < pmsh_skel.n / 2 && pmsh_skel.rows[2 * i] == j0; i++)
	{
		const u64 sk = pmsh_skel.rows[2 * i + 1];
		bool all = true;
		for (size_t k = 1; k < model->num_joints && all; k++)
		{
			unsigned long long jh;
			all = sscanf (model->joints[k].name, "joint_%16llx", &jh) == 1 && pmsh_skel_has (jh, sk);
		}
		if (!all)
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/../psks/%016llx.psks", dir, (unsigned long long)sk);
		u8 *d = 0;
		size_t n = 0;
		if (LoadFileAlloc (path, 0, 0, &d, &n, 16u << 20, 2, 0, false))
			continue;
		const uint m = PMSHApplySkeleton (model, d, (uint)n);
		FREE (d);
		if (m)
			return;
	}
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
	pmsh_bind_textures (model, arg, depth);
	{
		char dir[PATH_MAX];
		snprintf (dir, sizeof (dir), "%s", arg);
		char *sl = strrchr (dir, '/');
		if (sl)
			*sl = 0;
		else
			snprintf (dir, sizeof (dir), ".");
		pmsh_bind_skeleton (model, dir);
	}
	size_t tris = 0;
	for (size_t i = 0; i < model->num_meshes; i++)
		tris += model->meshes[i].num_vertices / 3;
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE PMSH:%s (%zu submeshes, %zu triangles, %zu joints) -> GLB:%s\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, model->num_meshes, tris, model->num_joints, glb);
	if (!testmode)
		ExportModelToGLB (model, glb);
	FreeModel (model);
	return ERR_OK;
}

// phx PANM animation: "<id>.panm" -> "<id>.glb" holding the skeleton (the PSKS from skeletons.tsv
// that has the most animated joints) and the clip.
static enumError extract_phx_panm_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".panm"))
		return ERR_NOTHING_TO_DO;
	u8 *d = 0;
	size_t n = 0;
	if (LoadFileAlloc (arg, 0, 0, &d, &n, 64u << 20, 2, 0, false))
		return ERR_NOTHING_TO_DO;
	if (!IsPANM (d, (uint)n))
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	char dir[PATH_MAX], glb[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s", arg);
	char *sl = strrchr (dir, '/');
	if (sl)
		*sl = 0;
	else
		snprintf (dir, sizeof (dir), ".");
	u64 *tg = 0;
	const uint nt = PANMTargets (d, (uint)n, &tg);
	u64 best = 0;
	uint best_n = 0;
	pmsh_skel_load (dir);
	for (uint t = 0; t < nt && t < 8 && pmsh_skel.n; t++)
	{
		// candidates: the skeletons of the first few targets
		uint lo = 0, hi = pmsh_skel.n / 2;
		while (lo < hi)
		{
			const uint mid = (lo + hi) / 2;
			if (pmsh_skel.rows[2 * mid] < tg[t])
				lo = mid + 1;
			else
				hi = mid;
		}
		for (uint i = lo; i < pmsh_skel.n / 2 && pmsh_skel.rows[2 * i] == tg[t]; i++)
		{
			const u64 sk = pmsh_skel.rows[2 * i + 1];
			uint m = 0;
			for (uint k = 0; k < nt; k++)
				m += pmsh_skel_has (tg[k], sk);
			if (m > best_n)
				best_n = m, best = sk;
		}
	}
	FREE (tg);
	u8 *sk = 0;
	size_t sk_n = 0;
	if (best_n)
	{
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/../psks/%016llx.psks", dir, (unsigned long long)best);
		if (LoadFileAlloc (path, 0, 0, &sk, &sk_n, 16u << 20, 2, 0, false))
			sk = 0;
	}
	char name[64];
	const char *base = strrchr (arg, '/');
	snprintf (name, sizeof (name), "%s", base ? base + 1 : arg);
	char *dot = strrchr (name, '.');
	if (dot)
		*dot = 0;
	uint matched = 0;
	model_t *model = DecodePANM (d, (uint)n, sk, (uint)sk_n, name, &matched);
	if (model && sk && matched < nt / 2)
	{
		// the best skeleton covers too little: keep every channel on flat joints instead
		FreeModel (model);
		model = DecodePANM (d, (uint)n, 0, 0, name, &matched);
		best_n = 0;
	}
	FREE (d);
	FREE (sk);
	if (!model)
		return depth > 0 ? ERR_OK : ERROR0 (ERR_INVALID_DATA, "PANM: no transform channels: %s\n", arg);
	beside_source_dest_ext (glb, sizeof (glb), arg, ".glb");
	if (verbose >= 0 || testmode)
	{
		fprintf (stdlog, "%s%sDECODE PANM:%s (%u channels, %zu joints", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, matched, model->num_joints);
		if (best_n)
			fprintf (stdlog, ", skeleton %016llx", (unsigned long long)best);
		fprintf (stdlog, ") -> GLB:%s\n", glb);
	}
	if (!testmode)
		ExportModelToGLB (model, glb);
	FreeModel (model);
	return ERR_OK;
}

// Kirby Air Riders "engine_bin" files: "startup.dat" (obfuscated MessagePack mount table) -> "startup.json",
// "Bin/config.bin" beside a "Prism_SB" schema.bin -> "config.prism" (the deobfuscated Prism data).
static enumError extract_phx_engine_bin_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	const bool startup = !strcasecmp (base, "startup.dat"), config = !strcasecmp (base, "config.bin");
	if (!startup && !config)
		return ERR_NOTHING_TO_DO;
	if (config)
	{
		char schema[PATH_MAX];
		snprintf (schema, sizeof (schema), "%.*sschema.bin", (int)(base - arg), arg);
		FILE *sf = fopen (schema, "rb");
		char magic[8] = "";
		const bool ok = sf && fread (magic, 1, 8, sf) == 8 && !memcmp (magic, "Prism_SB", 8);
		if (sf)
			fclose (sf);
		if (!ok)
			return ERR_NOTHING_TO_DO;
	}
	u8 *d = 0;
	size_t n = 0;
	if (LoadFileAlloc (arg, 0, 0, &d, &n, 64u << 20, 2, 0, false) || n < 2)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	u8 *dec = MALLOC (n);
	const uint m = dec ? PhxDeobfuscate (dec, d, (uint)n) : 0;
	FREE (d);
	if (!m || (startup && !PhxMsgpackToJSON (0, dec, m)))
	{
		FREE (dec);
		return ERR_NOTHING_TO_DO;
	}
	char out[PATH_MAX];
	beside_source_dest_ext (out, sizeof (out), arg, startup ? ".json" : ".prism");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE PHX ENGINE_BIN:%s (%u bytes) -> %s:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, m, startup ? "JSON" : "PRISM", out);
	enumError err = ERR_OK;
	if (!testmode)
	{
		FILE *f = fopen (out, startup ? "w" : "wb");
		if (!f)
			err = ERROR1 (ERR_CANT_CREATE, "Can't create file: %s\n", out);
		else
		{
			if (startup)
				PhxMsgpackToJSON (f, dec, m);
			else
				fwrite (dec, 1, m, f);
			fclose (f);
		}
	}
	FREE (dec);
	return err;
}
