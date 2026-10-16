// Speed 2 "DC2" world files (.dcw; Maps/F1_Track*.dcw): a scene description holding, for every mesh set, a position array
// {u32 0, u8 12, u32 n, n x f32 x3} directly followed by a UV array {u32 0, u8 8, u32 n, n x f32 x2} (the second copy of a
// pair is the z-mirrored track half), and later the primitive records of the sets in the same order: {u8 op, u16 n, n x
// {u16 position index, u16 normal index, u16 UV index}} with op 0x92 triangle list, 0x9a strip, 0x82 quads, 0xa2 fan. A
// set is closed by the run whose largest indices reach the last position and UV of the arrays. One mesh per set -> GLB
// (geometry only; materials / textures / instance transforms are not decoded).
typedef struct
{
	size_t pos, uv;
	u32 npos, nuv;
} dcw_set_t;

typedef struct
{
	size_t start, end;
	u32 maxa, maxc;
} dcw_run_t;

static u32 dcw_lim_a, dcw_lim_c;

static uint dcw_rec (const u8 *d, size_t sz, size_t o)
{
	const u8 op = d[o];
	if (op != 0x92 && op != 0x9a && op != 0x82 && op != 0xa2)
		return 0;
	const uint n = rd_be16 (d + o + 1);
	if (n < 3 || n >= 4000 || o + 3 + 6 * (size_t)n > sz || (op == 0x92 && n % 3) || (op == 0x82 && n % 4))
		return 0;
	for (uint i = 0; i < n; i++)
		if (rd_be16 (d + o + 3 + 6 * (size_t)i) >= dcw_lim_a || rd_be16 (d + o + 7 + 6 * (size_t)i) >= dcw_lim_c)
			return 0;
	return n;
}

static enumError extract_dcw_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".dcw") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *d = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &d, &sz, 0, 0, 0, false) || !d)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x100 || memcmp (d, "DC2\0", 4))
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}

	// position / UV array pairs
	dcw_set_t *sets = CALLOC (4096, sizeof (*sets));
	uint nsets = 0;
	size_t first_prim = 0;
	for (size_t p = 0x10; p + 9 < sz && sets && nsets < 4096;)
	{
		if (!rd_be32 (d + p) && d[p + 4] == 12)
		{
			const u32 n = rd_be32 (d + p + 5);
			const size_t q = p + 9 + 12 * (size_t)n;
			if (n && n < 0x100000 && q + 9 < sz && !rd_be32 (d + q) && d[q + 4] == 8)
			{
				const u32 m = rd_be32 (d + q + 5);
				if (m && m < 0x100000 && q + 9 + 8 * (size_t)m <= sz)
				{
					sets[nsets].pos = p + 9;
					sets[nsets].npos = n;
					sets[nsets].uv = q + 9;
					sets[nsets].nuv = m;
					nsets++;
					p = q + 9 + 8 * (size_t)m;
					first_prim = p;
					continue;
				}
			}
		}
		p++;
	}

	dcw_lim_a = dcw_lim_c = 0;
	for (uint i = 0; i < nsets; i++)
	{
		dcw_lim_a = sets[i].npos > dcw_lim_a ? sets[i].npos : dcw_lim_a;
		dcw_lim_c = sets[i].nuv > dcw_lim_c ? sets[i].nuv : dcw_lim_c;
	}
	// primitive runs behind the arrays
	dcw_run_t *runs = CALLOC (16384, sizeof (*runs));
	uint nruns = 0;
	for (size_t o = first_prim; runs && o + 9 < sz && nruns < 16384;)
	{
		uint n = dcw_rec (d, sz, o);
		if (!n)
		{
			o++;
			continue;
		}
		dcw_run_t r = {o, o, 0, 0};
		uint cnt = 0;
		while ((n = dcw_rec (d, sz, r.end)))
		{
			for (uint i = 0; i < n; i++)
			{
				const u8 *e = d + r.end + 3 + 6 * (size_t)i;
				if (rd_be16 (e) > r.maxa)
					r.maxa = rd_be16 (e);
				if (rd_be16 (e + 4) > r.maxc)
					r.maxc = rd_be16 (e + 4);
			}
			r.end += 3 + 6 * (size_t)n;
			cnt++;
		}
		if (cnt >= 1)
			runs[nruns++] = r;
		o = r.end > o ? r.end : o + 1;
	}

	model_t *model = CALLOC (1, sizeof (*model));
	uint cur = 0, nmesh = 0;
	u32 gma = 0, gmc = 0;
	size_t *rbeg = CALLOC (nsets + 1, sizeof (*rbeg));
	uint *rfirst = CALLOC (nsets + 1, sizeof (*rfirst)), *rlast = CALLOC (nsets + 1, sizeof (*rlast));
	if (model && sets && runs && rbeg && rfirst && rlast)
	{
		// assign runs to sets in order: a set is complete once its largest position and UV index are reached
		bool open = false;
		for (uint r = 0; r < nruns && cur < nsets; r++)
		{
			if (runs[r].maxa >= sets[cur].npos || runs[r].maxc >= sets[cur].nuv)
				continue; // not (or no longer) this set's
			if (!open)
			{
				rfirst[cur] = r;
				open = true;
				gma = gmc = 0;
			}
			rlast[cur] = r;
			gma = runs[r].maxa > gma ? runs[r].maxa : gma;
			gmc = runs[r].maxc > gmc ? runs[r].maxc : gmc;
			if (gma + 1 == sets[cur].npos && gmc + 1 == sets[cur].nuv)
			{
				open = false;
				// mesh of the runs rfirst..rlast that fit
				mesh_t *meshes = REALLOC (model->meshes, (model->num_meshes + 1) * sizeof (*meshes));
				if (!meshes)
					break;
				model->meshes = meshes;
				mesh_t *mesh = meshes + model->num_meshes;
				memset (mesh, 0, sizeof (*mesh));
				snprintf (mesh->name, sizeof (mesh->name), "set%u", cur);
				const dcw_set_t *s = sets + cur;
				mesh->positions = MALLOC (s->npos * sizeof (*mesh->positions));
				mesh->texcoords = MALLOC (s->nuv * sizeof (*mesh->texcoords));
				size_t cap = 0;
				for (uint q = rfirst[cur]; q <= rlast[cur]; q++)
					cap += (runs[q].end - runs[q].start) / 2;
				mesh->vertices = CALLOC (cap + 8, sizeof (*mesh->vertices));
				mesh->triangle_materials = CALLOC (cap / 3 + 8, sizeof (int));
				if (!mesh->positions || !mesh->texcoords || !mesh->vertices || !mesh->triangle_materials)
				{
					FREE (mesh->positions);
					FREE (mesh->texcoords);
					FREE (mesh->vertices);
					FREE (mesh->triangle_materials);
					break;
				}
				for (u32 i = 0; i < s->npos; i++)
				{
					union
					{
						u32 u;
						float f;
					} x, y, z;
					x.u = rd_be32 (d + s->pos + 12 * (size_t)i);
					y.u = rd_be32 (d + s->pos + 12 * (size_t)i + 4);
					z.u = rd_be32 (d + s->pos + 12 * (size_t)i + 8);
					mesh->positions[i].x = x.f;
					mesh->positions[i].y = y.f;
					mesh->positions[i].z = z.f;
				}
				for (u32 i = 0; i < s->nuv; i++)
				{
					union
					{
						u32 u;
						float f;
					} u, v;
					u.u = rd_be32 (d + s->uv + 8 * (size_t)i);
					v.u = rd_be32 (d + s->uv + 8 * (size_t)i + 4);
					mesh->texcoords[i].u = u.f;
					mesh->texcoords[i].v = v.f;
				}
				mesh->num_positions = s->npos;
				mesh->num_texcoords = s->nuv;
				size_t nv = 0;
				for (uint q = rfirst[cur]; q <= rlast[cur]; q++)
				{
					if (runs[q].maxa >= s->npos || runs[q].maxc >= s->nuv)
						continue;
					for (size_t o = runs[q].start; o < runs[q].end;)
					{
						const u8 op = d[o];
						const uint n = rd_be16 (d + o + 1);
						const u8 *e = d + o + 3;
						o += 3 + 6 * (size_t)n;
						for (uint i = 0; i + 2 < n || (op == 0x82 && i + 3 < n + 0); i++)
						{
							uint ix[3];
							if (op == 0x92)
							{
								if (i % 3)
									continue;
								ix[0] = i, ix[1] = i + 1, ix[2] = i + 2;
							}
							else if (op == 0x82)
							{
								if (i % 4 || i + 3 >= n)
									continue;
								// quad -> two triangles
								const uint qd[6] = {i, i + 1, i + 2, i + 2, i + 1, i + 3};
								for (uint k = 0; k < 6; k++)
								{
									vertex_t *vt = mesh->vertices + nv++;
									memset (vt, 0xff, sizeof (*vt));
									vt->position_idx = rd_be16 (e + 6 * (size_t)qd[k]);
									vt->texcoord_idx = rd_be16 (e + 6 * (size_t)qd[k] + 4);
								}
								continue;
							}
							else if (op == 0xa2)
								ix[0] = 0, ix[1] = i + 1, ix[2] = i + 2;
							else
								ix[0] = i, ix[1] = i + 1 + (i & 1), ix[2] = i + 2 - (i & 1);
							const u32 a = rd_be16 (e + 6 * (size_t)ix[0]), b = rd_be16 (e + 6 * (size_t)ix[1]),
								  c = rd_be16 (e + 6 * (size_t)ix[2]);
							if (a == b || b == c || a == c)
								continue;
							const u32 ids[3] = {a, b, c};
							const uint ui[3] = {ix[0], ix[1], ix[2]};
							for (uint k = 0; k < 3; k++)
							{
								vertex_t *vt = mesh->vertices + nv++;
								memset (vt, 0xff, sizeof (*vt));
								vt->position_idx = (int)ids[k];
								vt->texcoord_idx = rd_be16 (e + 6 * (size_t)ui[k] + 4);
							}
						}
					}
				}
				mesh->num_vertices = nv - nv % 3;
				model->num_meshes++;
				nmesh++;
				cur++;
			}
		}
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (model && nmesh)
	{
		char dest[PATH_MAX];
		beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");
		if (testmode)
			fprintf (stdlog, "WOULD EXTRACT DCW:%s -> GLB:%s\n", arg, dest);
		else
		{
			ExportModelToGLB (model, dest);
			if (verbose >= 0)
				fprintf (stdlog, "%sEXTRACT DCW:%s (%u meshes) -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, nmesh, dest);
		}
		err = ERR_OK;
	}
	FreeModel (model);
	FREE (rbeg);
	FREE (rfirst);
	FREE (rlast);
	FREE (sets);
	FREE (runs);
	FREE (d);
	return err;
}
