// Krome Studios ".mdl" / ".mdg" static models (Star Wars: The Force Unleashed, Wii): "MDL3" holds names and bounds, the
// "MDG3" companion the geometry as chunks {0x20 byte field block at f: u16 x4 (the 4th = strip count), u16 0xffff, u16
// flags, .., u32 colour table offset (from f + 0x20), u32 colour table size, u32 position array size, u32 UV array size;
// strips at f + 0x20: {u8 op (0x98 strip, 0x90 list, 0xa0 fan), u16 n, n x 9 byte records {u16 position index, s8 x3 normal
// (/ 64), [6 tangent bytes in the 15 byte records of op 0x9a / 0x92 / 0xa2 (skinned models; positions then have a 16 byte
// stride, no skinning is applied)], u16 colour index, u16 UV index}}; the colour table (rgba8), f32 x3 positions and s16 x2 UV (/ 4096) arrays follow
// each other}, the next chunk starts right behind the UV array. The texture is the first string of the .mdl that names an
// existing ".tex" beside it. Skinned (character) models use another layout and are left alone. -> "<name>.glb"
typedef struct
{
	u8 op;
	size_t at;
	uint n;
	size_t rs;
} kmdl_strip_t;

static bool kmdl_chunk (const u8 *d, size_t sz, size_t f, kmdl_strip_t *strips, uint *nstrips, size_t *tab, size_t *pos,
	size_t *uv, size_t *end, uint *maxa, uint *maxb, uint *maxc)
{
	if (f + 0x40 > sz)
		return false;
	const uint ns = rd_be16 (d + f + 6);
	const size_t s0 = f + 0x20, o = rd_be32 (d + f + 0x10), s1 = rd_be32 (d + f + 0x14), s2 = rd_be32 (d + f + 0x18),
		     s3 = rd_be32 (d + f + 0x1c);
	if (!ns || ns > 4000 || o > sz)
		return false;
	size_t q = s0;
	*maxa = *maxb = *maxc = 0;
	for (uint i = 0; i < ns; i++)
	{
		const u8 op = d[q] & ~2;
		if (q + 3 > sz || (op != 0x98 && op != 0x90 && op != 0xa0) || (d[q] & ~2) != op)
			return false;
		const size_t rs = d[q] & 2 ? 15 : 9;
		const uint n = rd_be16 (d + q + 1);
		if (q + 3 + rs * n > sz)
			return false;
		strips[i].op = op;
		strips[i].rs = rs;
		strips[i].at = q + 3;
		strips[i].n = n;
		for (uint r = 0; r < n; r++)
		{
			const u8 *rec = d + q + 3 + rs * (size_t)r;
			if (rd_be16 (rec) > *maxa)
				*maxa = rd_be16 (rec);
			if (rd_be16 (rec + rs - 4) > *maxb)
				*maxb = rd_be16 (rec + rs - 4);
			if (rd_be16 (rec + rs - 2) > *maxc)
				*maxc = rd_be16 (rec + rs - 2);
		}
		q += 3 + rs * (size_t)n;
	}
	*nstrips = ns;
	*tab = s0 + o;
	*pos = *tab + s1;
	*uv = *pos + s2;
	*end = *uv + s3;
	return *end <= sz && (*maxa + 1) * (strips[0].rs == 15 ? 16ull : 12ull) <= s2 && (*maxc + 1) * 4ull <= s3 && (*maxb + 1) * 4ull <= s1;
}

static enumError extract_kromemdl_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!(export_count > 0 || opt_decode) || !is_ext (arg, ".mdl"))
		return ERR_NOTHING_TO_DO;
	u8 *mdl = 0, *mdg = 0;
	size_t msz = 0, gsz = 0;
	char gpath[PATH_MAX];
	snprintf (gpath, sizeof (gpath), "%s", arg);
	snprintf (gpath + strlen (gpath) - 4, 5, ".mdg");
	if (LoadFileAlloc (arg, 0, 0, &mdl, &msz, 0, 0, 0, false) || !mdl || msz < 0x100 || memcmp (mdl, "MDL3", 4)
		|| LoadFileAlloc (gpath, 0, 0, &mdg, &gsz, 0, 0, 0, false) || !mdg || gsz < 0x60 || memcmp (mdg, "MDG3", 4))
	{
		FREE (mdl);
		FREE (mdg);
		return ERR_NOTHING_TO_DO;
	}

	// texture candidates: strings of the .mdl that name a .tex beside it
	char texnames[8][64];
	uint ntex = 0;
	char dir[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s", arg);
	char *sl = strrchr (dir, '/');
	if (sl)
		sl[1] = 0;
	else
		dir[0] = 0;
	for (size_t i = 0x60; i < msz && ntex < 8;)
	{
		size_t j = i;
		while (j < msz && mdl[j] >= 0x20 && mdl[j] < 0x7f)
			j++;
		if (j - i >= 3 && j - i < 60 && j < msz && !mdl[j])
		{
			char name[64], path[PATH_MAX];
			snprintf (name, sizeof (name), "%.*s", (int)(j - i), (ccp)mdl + i);
			snprintf (path, sizeof (path), "%s%s.tex", dir, name);
			bool dup = false;
			for (uint k = 0; k < ntex; k++)
				dup |= !strcmp (texnames[k], name);
			if (!dup && !access (path, R_OK))
				snprintf (texnames[ntex++], sizeof (texnames[0]), "%s", name);
		}
		i = j + 1;
	}

	model_t *model = CALLOC (1, sizeof (*model));
	kmdl_strip_t *strips = MALLOC (4000 * sizeof (*strips));
	uint nchunk = 0;
	size_t f = 0x20;
	if (model && strips)
	{
		model->materials = CALLOC (ntex + 1, sizeof (*model->materials));
		model->num_materials = model->materials ? ntex + 1 : 0;
		for (uint i = 0; model->materials && i <= ntex; i++)
		{
			material_t *m = model->materials + i;
			if (i < ntex)
			{
				snprintf (m->name, sizeof (m->name), "%s", texnames[i]);
				snprintf (m->textures[0], sizeof (m->textures[0]), "%s.png", texnames[i]);
				m->num_textures = 1;
			}
			else
				snprintf (m->name, sizeof (m->name), "untextured");
			m->diffuse[0] = m->diffuse[1] = m->diffuse[2] = m->diffuse[3] = 1.0f;
		}
		for (;;)
		{
			uint ns, ma, mb, mc;
			size_t tab, pos, uv, end;
			if (!kmdl_chunk (mdg, gsz, f, strips, &ns, &tab, &pos, &uv, &end, &ma, &mb, &mc))
				break;
			size_t ncorner = 0, cap = 0;
			for (uint s = 0; s < ns; s++)
				cap += strips[s].n * 3;
			vec3_t *P = MALLOC (cap * sizeof (*P) + 1), *N = MALLOC (cap * sizeof (*N) + 1);
			vec2_t *U = MALLOC (cap * sizeof (*U) + 1);
			color4_t *C = MALLOC (cap * sizeof (*C) + 1);
			vertex_t *V = CALLOC (cap + 1, sizeof (*V));
			int *tm = CALLOC (cap / 3 + 1, sizeof (*tm));
			mesh_t *meshes = REALLOC (model->meshes, (model->num_meshes + 1) * sizeof (*meshes));
			if (!P || !N || !U || !C || !V || !tm || !meshes)
			{
				FREE (P);
				FREE (N);
				FREE (U);
				FREE (C);
				FREE (V);
				FREE (tm);
				if (meshes)
					model->meshes = meshes;
				break;
			}
			const int mat = ntex ? (int)(nchunk < ntex ? nchunk : ntex - 1) : 0;
			size_t ntri = 0;
			for (uint s = 0; s < ns; s++)
			{
				const uint n = strips[s].n;
				for (uint r = 0; r + 2 < n + (strips[s].op == 0x90 ? 0 : 0); r++)
				{
					uint idx[3];
					switch (strips[s].op)
					{
						case 0x90:
							if (r % 3)
								continue;
							idx[0] = r, idx[1] = r + 1, idx[2] = r + 2;
							break;
						case 0xa0:
							idx[0] = 0, idx[1] = r + 1, idx[2] = r + 2;
							break;
						default:
							idx[0] = r, idx[1] = r + 1 + (r & 1), idx[2] = r + 2 - (r & 1);
					}
					vec3_t p[3], nn[3];
					vec2_t t[3];
					color4_t c[3];
					u32 pi[3];
					for (uint k = 0; k < 3; k++)
					{
						const u8 *rec = mdg + strips[s].at + strips[s].rs * (size_t)idx[k];
						pi[k] = rd_be16 (rec);
						const u8 *pp = mdg + pos + (strips[s].rs == 15 ? 16 : 12) * (size_t)pi[k];
						union
						{
							u32 u;
							float f;
						} x, y, z;
						x.u = rd_be32 (pp);
						y.u = rd_be32 (pp + 4);
						z.u = rd_be32 (pp + 8);
						p[k].x = x.f;
						p[k].y = y.f;
						p[k].z = z.f;
						nn[k].x = (s8)rec[2] / 64.0f;
						nn[k].y = (s8)rec[3] / 64.0f;
						nn[k].z = (s8)rec[4] / 64.0f;
						const u8 *cp = mdg + tab + 4 * (size_t)rd_be16 (rec + strips[s].rs - 4);
						c[k].r = cp[0] / 255.0f;
						c[k].g = cp[1] / 255.0f;
						c[k].b = cp[2] / 255.0f;
						c[k].a = cp[3] / 255.0f;
						const u8 *up = mdg + uv + 4 * (size_t)rd_be16 (rec + strips[s].rs - 2);
						t[k].u = (s16)rd_be16 (up) / 4096.0f;
						t[k].v = (s16)rd_be16 (up + 2) / 4096.0f;
					}
					if (pi[0] == pi[1] || pi[1] == pi[2] || pi[0] == pi[2])
						continue;
					// face the way the stored normals point
					const float ux = p[1].x - p[0].x, uy = p[1].y - p[0].y, uz = p[1].z - p[0].z, vx = p[2].x - p[0].x,
						    vy = p[2].y - p[0].y, vz = p[2].z - p[0].z;
					const float fx = uy * vz - uz * vy, fy = uz * vx - ux * vz, fz = ux * vy - uy * vx;
					const float sx = nn[0].x + nn[1].x + nn[2].x, sy = nn[0].y + nn[1].y + nn[2].y,
						    sz2 = nn[0].z + nn[1].z + nn[2].z;
					uint o2[3] = {0, 1, 2};
					if (fx * sx + fy * sy + fz * sz2 < 0)
						o2[1] = 2, o2[2] = 1;
					for (uint k = 0; k < 3; k++)
					{
						const uint j = o2[k];
						P[ncorner] = p[j];
						N[ncorner] = nn[j];
						U[ncorner] = t[j];
						C[ncorner] = c[j];
						memset (V + ncorner, 0xff, sizeof (*V));
						V[ncorner].position_idx = V[ncorner].normal_idx = V[ncorner].texcoord_idx
							= V[ncorner].color_idx[0] = (int)ncorner;
						ncorner++;
					}
					tm[ntri++] = mat;
				}
			}
			model->meshes = meshes;
			mesh_t *mesh = meshes + model->num_meshes++;
			memset (mesh, 0, sizeof (*mesh));
			snprintf (mesh->name, sizeof (mesh->name), "chunk%u", nchunk);
			mesh->positions = P;
			mesh->num_positions = ncorner;
			mesh->normals = N;
			mesh->num_normals = ncorner;
			mesh->texcoords = U;
			mesh->num_texcoords = ncorner;
			mesh->colors[0] = C;
			mesh->num_colors[0] = ncorner;
			mesh->vertices = V;
			mesh->num_vertices = ncorner;
			mesh->triangle_materials = tm;
			mesh->material_idx = mat;
			nchunk++;
			f = end;
		}
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (nchunk)
	{
		char glb[PATH_MAX];
		beside_source_dest_ext (glb, sizeof (glb), arg, ".glb");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT KROME-MDL:%s (%u chunks) -> GLB:%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, nchunk, glb);
		if (!testmode)
			ExportModelToGLB (model, glb);
		err = ERR_OK;
	}
	FREE (strips);
	FREE (mdl);
	FREE (mdg);
	FreeModel (model);
	return err;
}
