// Plain 3D mesh formats converted to a single-material GLB ("<name>.glb" beside the source): binary and ASCII STL (".stl", face normals kept), PLY (".ply": ascii, binary little /
// big endian; x y z, optional nx ny nz and s / u, t / v; polygons are fanned), Quake II MD2 (".md2", first frame) and Quake III MD3 (".md3", first frame, one mesh per surface).
typedef struct mc_mesh_t
{
	char name[64];
	vec3_t *pos; size_t npos;
	vec3_t *nrm; size_t nnrm;
	vec2_t *uv; size_t nuv;
	int *ip, *in, *it; // per corner indices (-1 = none)
	size_t nv;
} mc_mesh_t;

static void mc_free (mc_mesh_t *m, uint n)
{
	for (uint i = 0; i < n; i++)
	{
		FREE (m[i].pos); FREE (m[i].nrm); FREE (m[i].uv); FREE (m[i].ip); FREE (m[i].in); FREE (m[i].it);
	}
}

static enumError mc_emit (ccp arg, ccp basedir, mc_mesh_t *src, uint n, ccp label)
{
	enumError err = ERR_NOTHING_TO_DO;
	model_t *model = CALLOC (1, sizeof (*model));
	mesh_t *meshes = CALLOC (n ? n : 1, sizeof (*meshes));
	material_t *mat = CALLOC (1, sizeof (*mat));
	uint made = 0;
	for (uint i = 0; model && meshes && mat && i < n; i++)
	{
		if (!src[i].nv || !src[i].npos)
			continue;
		vertex_t *vt = CALLOC (src[i].nv, sizeof (*vt));
		if (!vt)
			continue;
		for (size_t k = 0; k < src[i].nv; k++)
		{
			vt[k].position_idx = src[i].ip[k];
			vt[k].normal_idx = src[i].in ? src[i].in[k] : -1;
			vt[k].tangent_idx = vt[k].matrix_idx = -1;
			vt[k].texcoord_idx = src[i].it ? src[i].it[k] : -1;
			vt[k].color_idx[0] = vt[k].color_idx[1] = -1;
			for (int x = 0; x < 7; x++)
				vt[k].extra_texcoord_idx[x] = -1;
		}
		mesh_t *m = meshes + made++;
		snprintf (m->name, sizeof (m->name), "%s", src[i].name);
		m->positions = src[i].pos; m->num_positions = src[i].npos;
		m->normals = src[i].nnrm ? src[i].nrm : 0; m->num_normals = src[i].nnrm;
		m->texcoords = src[i].nuv ? src[i].uv : 0; m->num_texcoords = src[i].nuv;
		m->vertices = vt; m->num_vertices = src[i].nv;
		m->material_idx = 0;
	}
	if (model && meshes && mat && made)
	{
		snprintf (mat->name, sizeof (mat->name), "default");
		mat->diffuse[0] = mat->diffuse[1] = mat->diffuse[2] = 0.8f;
		mat->diffuse[3] = 1.0f;
		model->meshes = meshes; model->num_meshes = made;
		model->materials = mat; model->num_materials = 1;
		char dest[PATH_MAX], path[PATH_MAX], base[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (base, sizeof (base), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (base, '.');
		if (dot) *dot = 0;
		snprintf (path, sizeof (path), "%s/%s%s.glb", dest, basedir ? basedir : "", base);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT %s:%s %u mesh(es) -> GLB:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", label, arg, made, path);
		if (!testmode)
		{
			char dir[PATH_MAX];
			snprintf (dir, sizeof (dir), "%s/%s", dest, basedir ? basedir : "");
			CreatePath (dir, true);
			err = ExportModelToGLB (model, path) ? ERR_CANT_CREATE : ERR_OK;
		}
		else
			err = ERR_OK;
	}
	FREE (meshes);
	FREE (mat);
	FREE (model);
	return err;
}

static float mc_f32 (const u8 *p, bool be)
{
	const u32 u = be ? rd_be32 (p) : (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
	float f;
	memcpy (&f, &u, 4);
	return f;
}

static enumError extract_meshconv_file (ccp arg, ccp basedir, uint depth)
{
	const bool stl = is_ext (arg, ".stl"), ply = is_ext (arg, ".ply"), md2 = is_ext (arg, ".md2"), md3 = is_ext (arg, ".md3");
	if ((!stl && !ply && !md2 && !md3) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	mc_mesh_t mesh[64];
	memset (mesh, 0, sizeof (mesh));
	uint nm = 0;
#define L32(p) ((u32)(p)[0] | (u32)(p)[1] << 8 | (u32)(p)[2] << 16 | (u32)(p)[3] << 24)
#define L16(p) ((uint)(p)[0] | (uint)(p)[1] << 8)
	if (stl && sz > 84)
	{
		const u32 nt = L32 (raw + 80);
		const bool binary = 84 + (size_t)nt * 50 == sz;
		mc_mesh_t *m = mesh;
		snprintf (m->name, sizeof (m->name), "stl");
		if (binary && nt)
		{
			m->pos = CALLOC (nt * 3, sizeof (vec3_t));
			m->nrm = CALLOC (nt, sizeof (vec3_t));
			m->ip = MALLOC (nt * 3 * sizeof (int));
			m->in = MALLOC (nt * 3 * sizeof (int));
			if (m->pos && m->nrm && m->ip && m->in)
			{
				for (u32 t = 0; t < nt; t++)
				{
					const u8 *r = raw + 84 + (size_t)t * 50;
					m->nrm[t] = (vec3_t){mc_f32 (r, false), mc_f32 (r + 4, false), mc_f32 (r + 8, false)};
					for (uint k = 0; k < 3; k++)
					{
						m->pos[t * 3 + k] = (vec3_t){mc_f32 (r + 12 + k * 12, false), mc_f32 (r + 16 + k * 12, false), mc_f32 (r + 20 + k * 12, false)};
						m->ip[t * 3 + k] = t * 3 + k;
						m->in[t * 3 + k] = t;
					}
				}
				m->npos = nt * 3; m->nnrm = nt; m->nv = nt * 3;
				nm = 1;
			}
		}
		else if (!memcmp (raw, "solid", 5))
		{
			// ASCII: facet normal nx ny nz / vertex x y z (3 times)
			char *t = MALLOC (sz + 1);
			size_t cap = 1024, nt = 0;
			m->pos = MALLOC (cap * 3 * sizeof (vec3_t));
			m->nrm = MALLOC (cap * sizeof (vec3_t));
			if (t && m->pos && m->nrm)
			{
				memcpy (t, raw, sz); t[sz] = 0;
				vec3_t fn = {0, 0, 0};
				uint vi = 0;
				for (char *l = t; l && *l;)
				{
					char *nl = strchr (l, '\n');
					if (nl) *nl = 0;
					while (*l == ' ' || *l == '\t') l++;
					if (!strncmp (l, "facet normal", 12)) { sscanf (l + 12, "%f %f %f", &fn.x, &fn.y, &fn.z); vi = 0; }
					else if (!strncmp (l, "vertex", 6) && vi < 3)
					{
						if (nt >= cap)
						{
							cap *= 2;
							m->pos = REALLOC (m->pos, cap * 3 * sizeof (vec3_t));
							m->nrm = REALLOC (m->nrm, cap * sizeof (vec3_t));
							if (!m->pos || !m->nrm) break;
						}
						vec3_t v;
						sscanf (l + 6, "%f %f %f", &v.x, &v.y, &v.z);
						m->pos[nt * 3 + vi] = v;
						if (++vi == 3) { m->nrm[nt] = fn; nt++; }
					}
					l = nl ? nl + 1 : 0;
				}
				m->ip = MALLOC (nt * 3 * sizeof (int) + 4);
				m->in = MALLOC (nt * 3 * sizeof (int) + 4);
				if (m->ip && m->in && nt)
				{
					for (size_t k = 0; k < nt * 3; k++) { m->ip[k] = k; m->in[k] = k / 3; }
					m->npos = nt * 3; m->nnrm = nt; m->nv = nt * 3;
					nm = 1;
				}
			}
			FREE (t);
		}
	}
	else if (ply && sz > 30 && !memcmp (raw, "ply\n", 4) || (ply && sz > 30 && !memcmp (raw, "ply\r\n", 5)))
	{
		// header
		size_t p = 0;
		int fmt = -1; // 0 ascii, 1 little, 2 big
		uint nvx = 0, nfc = 0;
		struct { char name[16]; uint size; bool isfloat; } vprop[24];
		uint nprop = 0;
		char fc_cnt = 0, fc_idx = 0;
		int cur = 0;
		bool done = false;
		for (; p < sz && !done;)
		{
			size_t e = p;
			while (e < sz && raw[e] != '\n') e++;
			char line[256];
			snprintf (line, sizeof (line), "%.*s", (int)(e - p < 255 ? e - p : 255), raw + p);
			char *cr = strchr (line, '\r');
			if (cr) *cr = 0;
			p = e + 1;
			char a[64], b[64], c[64], d[64];
			const int n = sscanf (line, "%63s %63s %63s %63s", a, b, c, d);
			if (!strcmp (a, "format") && n >= 2) fmt = !strcmp (b, "ascii") ? 0 : !strcmp (b, "binary_little_endian") ? 1 : !strcmp (b, "binary_big_endian") ? 2 : -1;
			else if (!strcmp (a, "element") && n >= 3) { cur = !strcmp (b, "vertex") ? 1 : !strcmp (b, "face") ? 2 : 3; if (cur == 1) nvx = atoi (c); else if (cur == 2) nfc = atoi (c); }
			else if (!strcmp (a, "property") && n >= 3)
			{
				if (cur == 1 && nprop < 24)
				{
					const char *ty = b;
					const uint sz1 = !strcmp (ty, "char") || !strcmp (ty, "uchar") || !strcmp (ty, "int8") || !strcmp (ty, "uint8") ? 1 : !strcmp (ty, "short") || !strcmp (ty, "ushort") || !strcmp (ty, "int16") || !strcmp (ty, "uint16") ? 2 : !strcmp (ty, "double") || !strcmp (ty, "float64") ? 8 : 4;
					snprintf (vprop[nprop].name, sizeof (vprop[nprop].name), "%.15s", c);
					vprop[nprop].size = sz1;
					vprop[nprop].isfloat = !strcmp (ty, "float") || !strcmp (ty, "float32") || !strcmp (ty, "double") || !strcmp (ty, "float64");
					nprop++;
				}
				else if (cur == 2 && !strcmp (b, "list") && n >= 4)
				{
					char t3[64] = "";
					sscanf (line, "property list %63s %63s", c, t3);
					fc_cnt = !strcmp (c, "uchar") || !strcmp (c, "uint8") || !strcmp (c, "char") ? 1 : !strcmp (c, "ushort") || !strcmp (c, "short") ? 2 : 4;
					fc_idx = !strcmp (t3, "uchar") || !strcmp (t3, "uint8") ? 1 : !strcmp (t3, "ushort") || !strcmp (t3, "short") ? 2 : 4;
				}
			}
			else if (!strcmp (a, "end_header")) done = true;
		}
		if (done && fmt >= 0 && nvx && nvx < 20000000 && nfc < 20000000 && nprop >= 3)
		{
			mc_mesh_t *m = mesh;
			snprintf (m->name, sizeof (m->name), "ply");
			m->pos = CALLOC (nvx, sizeof (vec3_t));
			bool hn = false, hu = false;
			for (uint i = 0; i < nprop; i++)
			{
				if (!strcmp (vprop[i].name, "nx")) hn = true;
				if (!strcmp (vprop[i].name, "s") || !strcmp (vprop[i].name, "u") || !strcmp (vprop[i].name, "texture_u")) hu = true;
			}
			m->nrm = hn ? CALLOC (nvx, sizeof (vec3_t)) : 0;
			m->uv = hu ? CALLOC (nvx, sizeof (vec2_t)) : 0;
			size_t icap = (size_t)nfc * 6 + 6, nvv = 0;
			m->ip = MALLOC (icap * sizeof (int));
			bool ok = m->pos && m->ip && (!hn || m->nrm) && (!hu || m->uv);
			char *tx = 0;
			char *tp = 0;
			if (fmt == 0 && ok)
			{
				tx = MALLOC (sz - p + 1);
				if (tx) { memcpy (tx, raw + p, sz - p); tx[sz - p] = 0; tp = tx; } else ok = false;
			}
			size_t bp = p;
			for (uint v = 0; ok && v < nvx; v++)
			{
				double val[24] = {0};
				for (uint i = 0; i < nprop && ok; i++)
				{
					if (fmt == 0)
					{
						char *end;
						val[i] = strtod (tp, &end);
						if (end == tp) { ok = false; break; }
						tp = end;
					}
					else
					{
						if (bp + vprop[i].size > sz) { ok = false; break; }
						const bool be = fmt == 2;
						const u8 *q = raw + bp;
						if (vprop[i].isfloat)
						{
							if (vprop[i].size == 4) val[i] = mc_f32 (q, be);
							else { uint64_t u = 0; for (uint k = 0; k < 8; k++) u = be ? u << 8 | q[k] : u | (uint64_t)q[k] << (8 * k); double dd; memcpy (&dd, &u, 8); val[i] = dd; }
						}
						else
						{
							uint64_t u = 0;
							for (uint k = 0; k < vprop[i].size; k++) u = be ? u << 8 | q[k] : u | (uint64_t)q[k] << (8 * k);
							val[i] = (double)u;
						}
						bp += vprop[i].size;
					}
				}
				if (!ok) break;
				for (uint i = 0; i < nprop; i++)
				{
					const char *nme = vprop[i].name;
					if (!strcmp (nme, "x")) m->pos[v].x = val[i]; else if (!strcmp (nme, "y")) m->pos[v].y = val[i]; else if (!strcmp (nme, "z")) m->pos[v].z = val[i];
					else if (m->nrm && !strcmp (nme, "nx")) m->nrm[v].x = val[i]; else if (m->nrm && !strcmp (nme, "ny")) m->nrm[v].y = val[i]; else if (m->nrm && !strcmp (nme, "nz")) m->nrm[v].z = val[i];
					else if (m->uv && (!strcmp (nme, "s") || !strcmp (nme, "u") || !strcmp (nme, "texture_u"))) m->uv[v].u = val[i];
					else if (m->uv && (!strcmp (nme, "t") || !strcmp (nme, "v") || !strcmp (nme, "texture_v"))) m->uv[v].v = val[i];
				}
			}
			for (uint f = 0; ok && f < nfc && fc_cnt; f++)
			{
				uint cnt = 0, idx[256];
				if (fmt == 0)
				{
					char *end;
					cnt = strtoul (tp, &end, 10);
					if (end == tp || cnt > 255) { ok = false; break; }
					tp = end;
					for (uint k = 0; k < cnt; k++) { idx[k] = strtoul (tp, &end, 10); if (end == tp) { ok = false; break; } tp = end; }
				}
				else
				{
					const bool be = fmt == 2;
					if (bp + fc_cnt > sz) { ok = false; break; }
					for (uint k = 0; k < fc_cnt; k++) cnt = be ? cnt << 8 | raw[bp + k] : cnt | raw[bp + k] << (8 * k);
					bp += fc_cnt;
					if (cnt > 255 || bp + (size_t)cnt * fc_idx > sz) { ok = false; break; }
					for (uint k = 0; k < cnt; k++)
					{
						uint u = 0;
						for (uint b = 0; b < fc_idx; b++) u = be ? u << 8 | raw[bp + b] : u | raw[bp + b] << (8 * b);
						idx[k] = u; bp += fc_idx;
					}
				}
				if (!ok) break;
				for (uint k = 1; k + 1 < cnt; k++)
				{
					if (idx[0] >= nvx || idx[k] >= nvx || idx[k + 1] >= nvx) { ok = false; break; }
					if (nvv + 3 > icap) { icap *= 2; m->ip = REALLOC (m->ip, icap * sizeof (int)); if (!m->ip) { ok = false; break; } }
					m->ip[nvv++] = idx[0]; m->ip[nvv++] = idx[k]; m->ip[nvv++] = idx[k + 1];
				}
				// the facet tail (extra per face properties) is not supported; the list must be the only face property
			}
			FREE (tx);
			if (ok && nvv)
			{
				m->npos = nvx; m->nnrm = m->nrm ? nvx : 0; m->nuv = m->uv ? nvx : 0; m->nv = nvv;
				m->in = m->nrm ? MALLOC (nvv * sizeof (int)) : 0;
				m->it = m->uv ? MALLOC (nvv * sizeof (int)) : 0;
				for (size_t k = 0; k < nvv; k++)
				{
					if (m->in) m->in[k] = m->ip[k];
					if (m->it) m->it[k] = m->ip[k];
				}
				nm = 1;
			}
		}
	}
	else if (md2 && sz > 68 && !memcmp (raw, "IDP2", 4) && L32 (raw + 4) == 8)
	{
		const uint sw = L32 (raw + 8), sh = L32 (raw + 12), nv = L32 (raw + 20), nst = L32 (raw + 24), nt = L32 (raw + 28), nfr = L32 (raw + 40);
		const u32 ost = L32 (raw + 44), otr = L32 (raw + 48), ofr = L32 (raw + 56), fsz = L32 (raw + 36);
		if (sw && sh && nv && nt && nfr && nv < 65536 && nt < 200000 && (size_t)ofr + fsz <= sz && (size_t)ost + (size_t)nst * 4 <= sz && (size_t)otr + (size_t)nt * 12 <= sz && fsz >= 40 + nv * 4)
		{
			mc_mesh_t *m = mesh;
			snprintf (m->name, sizeof (m->name), "%.15s", raw + ofr + 24);
			m->pos = MALLOC (nv * sizeof (vec3_t));
			m->uv = MALLOC (nst ? nst * sizeof (vec2_t) : 8);
			m->ip = MALLOC (nt * 3 * sizeof (int));
			m->it = MALLOC (nt * 3 * sizeof (int));
			if (m->pos && m->uv && m->ip && m->it)
			{
				const u8 *fr = raw + ofr;
				const float sc[3] = {mc_f32 (fr, false), mc_f32 (fr + 4, false), mc_f32 (fr + 8, false)}, tr[3] = {mc_f32 (fr + 12, false), mc_f32 (fr + 16, false), mc_f32 (fr + 20, false)};
				for (uint i = 0; i < nv; i++)
				{
					const u8 *v = fr + 40 + i * 4;
					// MD2 is Z up: swap to Y up (x, z, -y)
					const float x = v[0] * sc[0] + tr[0], y = v[1] * sc[1] + tr[1], z = v[2] * sc[2] + tr[2];
					m->pos[i] = (vec3_t){x, z, -y};
				}
				for (uint i = 0; i < nst; i++)
				{
					const u8 *t = raw + ost + i * 4;
					m->uv[i] = (vec2_t){(float)L16 (t) / sw, (float)L16 (t + 2) / sh};
				}
				bool ok = true;
				for (uint t = 0; t < nt; t++)
				{
					const u8 *r = raw + otr + t * 12;
					for (uint k = 0; k < 3; k++)
					{
						const uint vi = L16 (r + (2 - k) * 2), ti = L16 (r + 6 + (2 - k) * 2);
						if (vi >= nv || ti >= nst) { ok = false; break; }
						m->ip[t * 3 + k] = vi;
						m->it[t * 3 + k] = ti;
					}
				}
				if (ok) { m->npos = nv; m->nuv = nst; m->nv = nt * 3; nm = 1; }
			}
		}
	}
	else if (md3 && sz > 108 && !memcmp (raw, "IDP3", 4))
	{
		const uint ns = L32 (raw + 84);
		const u32 osf = L32 (raw + 100);
		size_t sp = osf;
		for (uint s = 0; s < ns && s < 64 && sp + 108 <= sz; s++)
		{
			const u8 *h = raw + sp;
			if (memcmp (h, "IDP3", 4)) break;
			const uint nfr = L32 (h + 72), nvx = L32 (h + 80), nt = L32 (h + 84);
			const u32 otr = L32 (h + 88), ost = L32 (h + 92), ovx = L32 (h + 96), oend = L32 (h + 100);
			if (!nfr || !nvx || !nt || nvx > 65535 || nt > 200000 || sp + otr + (size_t)nt * 12 > sz || sp + ost + (size_t)nvx * 8 > sz || sp + ovx + (size_t)nvx * 8 > sz) { sp += oend; continue; }
			mc_mesh_t *m = mesh + nm;
			snprintf (m->name, sizeof (m->name), "%.63s", h + 4);
			m->pos = MALLOC (nvx * sizeof (vec3_t));
			m->uv = MALLOC (nvx * sizeof (vec2_t));
			m->ip = MALLOC (nt * 3 * sizeof (int));
			m->it = MALLOC (nt * 3 * sizeof (int));
			bool ok = m->pos && m->uv && m->ip && m->it;
			for (uint i = 0; ok && i < nvx; i++)
			{
				const u8 *v = raw + sp + ovx + i * 8;
				const float x = (int16_t)L16 (v) / 64.0f, y = (int16_t)L16 (v + 2) / 64.0f, z = (int16_t)L16 (v + 4) / 64.0f;
				m->pos[i] = (vec3_t){x, z, -y};
				const u8 *t = raw + sp + ost + i * 8;
				m->uv[i] = (vec2_t){mc_f32 (t, false), mc_f32 (t + 4, false)};
			}
			for (uint t = 0; ok && t < nt; t++)
				for (uint k = 0; k < 3; k++)
				{
					const uint vi = L32 (raw + sp + otr + t * 12 + (2 - k) * 4);
					if (vi >= nvx) { ok = false; break; }
					m->ip[t * 3 + k] = vi;
					m->it[t * 3 + k] = vi;
				}
			if (ok) { m->npos = nvx; m->nuv = nvx; m->nv = nt * 3; nm++; }
			else { FREE (m->pos); FREE (m->uv); FREE (m->ip); FREE (m->it); memset (m, 0, sizeof (*m)); }
			sp += oend;
		}
	}
#undef L32
#undef L16
	if (nm)
		err = mc_emit (arg, basedir, mesh, nm, stl ? "STL" : ply ? "PLY" : md2 ? "MD2" : "MD3");
	mc_free (mesh, 64);
	FREE (raw);
	return err;
}
