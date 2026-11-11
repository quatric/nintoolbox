// Kylotonn engine ".mdl" models (Cocoto Festival, Wii): u16, u16, length-prefixed name, then little-endian chunks
// {tag, u32 size incl. 8 header bytes}: FLAG DYNL CYLD SKIN{VERS PACK SCEN{... MATE OBJE{u32 count, MODL...} HIER} ...}.
// Every "MODL" has NAME PACK FLAG HIER COLL ZLIM BARI CENT SPHE VERT [NPTS] FACE PFCS QUAT OTRE. Geometry blocks are "u32 count" followed by a
// pack3 LZ stream (see kylotonn_txc.inc; the MODL's "PACK" 5 = Huffman coded, "PACK" 3 = plain LZ):
//   VERT: count x 3 little-endian floats;
//   FACE: count x 52 bytes, u16 vertex count of the polygon at +4, u32 first PFCS entry at +40, u32 material index at +44;
//   PFCS: one 44 byte record per polygon corner, u32 vertex index at +0 (UV and flag fields follow, not used).
// Each polygon is a fan; all MODLs (including the box helpers "..._Reference") become meshes of "<file>.d/<name>.glb". Materials and
// transforms are not applied: the MTRD bitmaps use another codec and HIER/QUAT are not decoded yet.
static u8 *kyl_mdl_block (const u8 *p, size_t n, size_t rec, u32 *count, uint pack)
{
	if (n < 4)
		return 0;
	*count = kyl_le32 (p);
	if (!*count || *count > 0x400000)
		return 0;
	return pack == 5 ? kyl_unpack5 (p + 4, n - 4, (size_t)*count * rec) : kyl_unpack3 (p + 4, n - 4, (size_t)*count * rec, false);
}

static enumError extract_kylotonn_mdl_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".mdl") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t obje = 0;
	if (sz > 0x40 && raw[4] && raw[4] + 5u < sz)
		for (size_t i = 5 + raw[4]; i + 16 < sz; i++)
			if (!memcmp (raw + i, "OBJE", 4) && kyl_le32 (raw + i + 4) <= sz - i && kyl_le32 (raw + i + 4) > 16)
			{
				obje = i;
				break;
			}
	enumError err = ERR_NOTHING_TO_DO;
	if (obje)
	{
		const size_t oend = obje + kyl_le32 (raw + obje + 4);
		uint nmodl = 0;
		for (size_t i = obje + 12; i + 8 <= oend;)
		{
			const u32 cs = kyl_le32 (raw + i + 4);
			if (cs < 8 || i + cs > oend)
				break;
			nmodl += !memcmp (raw + i, "MODL", 4);
			i += cs;
		}
		model_t *model = CALLOC (1, sizeof (*model));
		mesh_t *meshes = CALLOC (nmodl ? nmodl : 1, sizeof (*meshes));
		material_t *mat = CALLOC (1, sizeof (*mat));
		uint made = 0;
		for (size_t i = obje + 12; model && meshes && mat && i + 8 <= oend;)
		{
			const u32 cs = kyl_le32 (raw + i + 4);
			if (cs < 8 || i + cs > oend)
				break;
			if (!memcmp (raw + i, "MODL", 4))
			{
				const u8 *vp = 0, *fp = 0, *pp = 0;
				size_t vn = 0, fn = 0, pn = 0;
				char name[64] = "mesh";
				uint pack = 3;
				for (size_t c = i + 8; c + 8 <= i + cs;)
				{
					const u32 ccs = kyl_le32 (raw + c + 4);
					if (ccs < 8 || c + ccs > i + cs)
						break;
					const u8 *pl = raw + c + 8;
					if (!memcmp (raw + c, "VERT", 4) && !vp) { vp = pl; vn = ccs - 8; }
					else if (!memcmp (raw + c, "FACE", 4) && !fp) { fp = pl; fn = ccs - 8; }
					else if (!memcmp (raw + c, "PFCS", 4) && !pp) { pp = pl; pn = ccs - 8; }
					else if (!memcmp (raw + c, "PACK", 4) && ccs >= 12)
						pack = kyl_le32 (pl);
					else if (!memcmp (raw + c, "NAME", 4) && ccs >= 14 && pl[4] && pl[4] + 5u <= ccs - 8)
					{
						snprintf (name, sizeof (name), "%.*s", (int)pl[4], (const char *)pl + 5);
					}
					c += ccs;
				}
				u32 nv = 0, nf = 0, np = 0;
				u8 *V = vp ? kyl_mdl_block (vp, vn, 12, &nv, pack) : 0;
				u8 *F = fp ? kyl_mdl_block (fp, fn, 52, &nf, pack) : 0;
				u8 *P = pp ? kyl_mdl_block (pp, pn, 44, &np, pack) : 0;
				size_t ntri = 0;
				bool ok = V && F && P;
				for (u32 f = 0; ok && f < nf; f++)
				{
					const uint cnt = F[f * 52 + 4] | F[f * 52 + 5] << 8;
					const u32 first = kyl_le32 (F + f * 52 + 40);
					if ((size_t)first + cnt > np)
						ok = false;
					else if (cnt >= 3)
						ntri += cnt - 2;
				}
				for (u32 k = 0; ok && k < np; k++)
					if (kyl_le32 (P + k * 44) >= nv)
						ok = false;
				if (ok && ntri)
				{
					vec3_t *pos = MALLOC ((size_t)nv * sizeof (*pos));
					vertex_t *vt = CALLOC (ntri * 3, sizeof (*vt));
					if (pos && vt)
					{
						for (u32 k = 0; k < nv; k++)
						{
							float xyz[3];
							for (int a = 0; a < 3; a++)
							{
								const u32 bits = kyl_le32 (V + k * 12 + a * 4);
								memcpy (&xyz[a], &bits, 4);
							}
							pos[k].x = xyz[0]; pos[k].y = xyz[1]; pos[k].z = xyz[2];
						}
						size_t t = 0;
						for (u32 f = 0; f < nf; f++)
						{
							const uint cnt = F[f * 52 + 4] | F[f * 52 + 5] << 8;
							const u32 first = kyl_le32 (F + f * 52 + 40);
							for (uint j = 1; j + 1 < cnt; j++)
							{
								const u32 idx[3] = { kyl_le32 (P + (first) * 44), kyl_le32 (P + (first + j) * 44), kyl_le32 (P + (first + j + 1) * 44) };
								for (int a = 0; a < 3; a++)
								{
									vertex_t *v = vt + t * 3 + a;
									v->position_idx = (int)idx[a];
									v->normal_idx = v->tangent_idx = v->texcoord_idx = v->matrix_idx = -1;
									v->color_idx[0] = v->color_idx[1] = -1;
									for (int x = 0; x < 7; x++)
										v->extra_texcoord_idx[x] = -1;
								}
								t++;
							}
						}
						mesh_t *m = meshes + made++;
						snprintf (m->name, sizeof (m->name), "%s", name);
						m->positions = pos;
						m->num_positions = nv;
						m->vertices = vt;
						m->num_vertices = ntri * 3;
						m->material_idx = 0;
					}
					else
					{
						FREE (pos);
						FREE (vt);
					}
				}
				FREE (V);
				FREE (F);
				FREE (P);
			}
			i += cs;
		}
		if (model && meshes && mat && made)
		{
			snprintf (mat->name, sizeof (mat->name), "default");
			model->meshes = meshes;
			model->num_meshes = made;
			model->materials = mat;
			model->num_materials = 1;
			char dest[PATH_MAX], path[PATH_MAX], base[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (base, sizeof (base), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (base, '.');
			if (dot)
				*dot = 0;
			snprintf (path, sizeof (path), "%s/%s%s.glb", dest, basedir ? basedir : "", base);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT KYLOTONN MODEL:%s %u meshes -> GLB:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, made, path);
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
		for (uint i = 0; i < made; i++)
		{
			FREE (meshes[i].positions);
			FREE (meshes[i].vertices);
		}
		FREE (meshes);
		FREE (mat);
		FREE (model);
	}
	FREE (raw);
	return err;
}
