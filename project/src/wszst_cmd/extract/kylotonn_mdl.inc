// Kylotonn engine ".mdl" models (Cocoto Festival, Wii): u16, u16, length-prefixed name, then little-endian chunks
// {tag, u32 size incl. 8 header bytes}: FLAG DYNL CYLD SKIN{VERS PACK SCEN{... MATE OBJE{u32 count, MODL...} HIER} ...}.
// Every "MODL" has NAME PACK FLAG HIER COLL ZLIM BARI CENT SPHE VERT [NPTS] FACE PFCS QUAT OTRE. Geometry blocks are "u32 count" followed by a
// pack3 LZ stream (see kylotonn_txc.inc; the MODL's "PACK" 5 = Huffman coded, "PACK" 3 = plain LZ):
//   VERT: count x 3 little-endian floats;
//   FACE: count x 52 bytes, u16 vertex count of the polygon at +4, u32 first PFCS entry at +40, u32 material index at +44;
//   PFCS: one 44 byte record per polygon corner, u32 vertex index at +0, float u at +12 and v at +28 (in texels of a 256 pixel texture).
// Each polygon is a fan; all MODLs (including the box helpers "..._Reference") become meshes of "<file>.d/<name>.glb". UVs are divided by 256. Materials and
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
		// material names: the NAMEs of the MTRD blocks of "MATE" (they are the texture file names)
		char matname[16][64];
		uint nmat = 0;
		for (size_t i = 5 + raw[4]; i + 12 < obje && nmat < 16; i++)
			if (!memcmp (raw + i, "MATE", 4) && kyl_le32 (raw + i + 4) <= obje - i && kyl_le32 (raw + i + 4) > 12)
			{
				const size_t mend = i + kyl_le32 (raw + i + 4);
				for (size_t m = i + 12; m + 8 <= mend && nmat < 16;)
				{
					const u32 ms = kyl_le32 (raw + m + 4);
					if (ms < 8 || m + ms > mend)
						break;
					if (!memcmp (raw + m, "MTRD", 4))
					{
						matname[nmat][0] = 0;
						for (size_t c = m + 8; c + 8 <= m + ms;)
						{
							const u32 cs = kyl_le32 (raw + c + 4);
							if (cs < 8 || c + cs > m + ms)
								break;
							if (!memcmp (raw + c, "NAME", 4) && cs >= 14 && raw[c + 12] && raw[c + 12] + 5u <= cs - 8)
								snprintf (matname[nmat], 64, "%.*s", (int)raw[c + 12], (const char *)raw + c + 13);
							c += cs;
						}
						nmat++;
					}
					m += ms;
				}
				break;
			}
		model_t *model = CALLOC (1, sizeof (*model));
		mesh_t *meshes = CALLOC (nmodl ? nmodl : 1, sizeof (*meshes));
		material_t *mat = CALLOC (nmat ? nmat : 1, sizeof (*mat));
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
					int *tmat = nmat > 1 ? CALLOC (ntri, sizeof (*tmat)) : 0;
					vec2_t *uv = MALLOC ((size_t)np * sizeof (*uv));
					if (pos && vt && uv)
					{
						for (u32 k = 0; k < np; k++)
						{
							const u32 ub = kyl_le32 (P + k * 44 + 12), vb = kyl_le32 (P + k * 44 + 28);
							float fu, fv;
							memcpy (&fu, &ub, 4);
							memcpy (&fv, &vb, 4);
							uv[k].u = fu / 256.0f; // texel units of the 256 pixel textures
							uv[k].v = fv / 256.0f;
						}
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
								const u32 corner[3] = { first, first + j, first + j + 1 };
								const u32 idx[3] = { kyl_le32 (P + corner[0] * 44), kyl_le32 (P + corner[1] * 44), kyl_le32 (P + corner[2] * 44) };
								for (int a = 0; a < 3; a++)
								{
									vertex_t *v = vt + t * 3 + a;
									v->position_idx = (int)idx[a];
									v->normal_idx = v->tangent_idx = v->matrix_idx = -1;
									v->texcoord_idx = (int)corner[a];
									v->color_idx[0] = v->color_idx[1] = -1;
									for (int x = 0; x < 7; x++)
										v->extra_texcoord_idx[x] = -1;
								}
								if (tmat)
								{
									const u32 mi = kyl_le32 (F + f * 52 + 44);
									tmat[t] = mi < nmat ? (int)mi : 0;
								}
								t++;
							}
						}
						mesh_t *m = meshes + made++;
						snprintf (m->name, sizeof (m->name), "%s", name);
						m->positions = pos;
						m->num_positions = nv;
						m->texcoords = uv;
						m->num_texcoords = np;
						m->vertices = vt;
						m->triangle_materials = tmat;
						m->num_vertices = ntri * 3;
						m->material_idx = 0;
					}
					else
					{
						FREE (pos);
						FREE (vt);
						FREE (uv);
						FREE (tmat);
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
			for (uint i = 0; i < nmat; i++)
			{
				snprintf (mat[i].name, sizeof (mat[i].name), "%s", matname[i][0] ? matname[i] : "material");
				if (matname[i][0])
				{
					snprintf (mat[i].textures[0], sizeof (mat[i].textures[0]), "%s", matname[i]);
					mat[i].num_textures = 1;
					mat[i].wrap_s[0] = mat[i].wrap_t[0] = 1;
					mat[i].min_filter[0] = mat[i].mag_filter[0] = 1;
				}
			}
			model->meshes = meshes;
			model->num_meshes = made;
			model->materials = mat;
			model->num_materials = nmat ? nmat : 1;
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
				// the textures live in "textures_wii/<name>.txc" next to the "models" directory: PNGs beside the GLB are embedded
				for (uint i = 0; i < nmat; i++)
				{
					if (!matname[i][0])
						continue;
					char probe[PATH_MAX], tx[PATH_MAX];
					snprintf (probe, sizeof (probe), "%s", arg);
					for (int up = 0; up < 5; up++)
					{
						char *sl2 = strrchr (probe, '/');
						if (!sl2)
							break;
						*sl2 = 0;
						snprintf (tx, sizeof (tx), "%s/textures_wii/%s.txc", probe, matname[i]);
						u8 *tr = 0;
						size_t ts = 0;
						if (!access (tx, R_OK) && !LoadFileAlloc (tx, 0, 0, &tr, &ts, 0, 0, 0, false) && tr)
						{
							uint tw = 0, th = 0;
							u8 *rgba = kyl_txc_rgba (tr, ts, &tw, &th);
							if (rgba)
							{
								kyl_write_rgba_png (arg, basedir, matname[i], (uint)-1, tw, th, rgba);
								FREE (rgba);
							}
							FREE (tr);
							break;
						}
						FREE (tr);
					}
				}
				err = ExportModelToGLB (model, path) ? ERR_CANT_CREATE : ERR_OK;
			}
			else
				err = ERR_OK;
		}
		for (uint i = 0; i < made; i++)
		{
			FREE (meshes[i].positions);
			FREE (meshes[i].texcoords);
			FREE (meshes[i].triangle_materials);
			FREE (meshes[i].vertices);
		}
		FREE (meshes);
		FREE (mat);
		FREE (model);
	}
	FREE (raw);
	return err;
}
