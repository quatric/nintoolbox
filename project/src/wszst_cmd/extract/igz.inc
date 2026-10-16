// Alchemy "IGZ" object files (Madagascar 2 Wii, Credits/level ".bld" members of IGA archives): big endian
// header {"IGZ\x01", u32 version, ... 4 x {u32 offset, size, align, 0} sections at 0x10}. Section 0 holds fixup
// records {u32 tag, ., ., count, length, start}: tag 0 type names, 13 type sizes, 5 object offsets, 12/11 the
// fields that reference pooled memory blocks and the blocks themselves {u32 size, u32 pool << 24 | offset} (pool 2
// starts at the offset of section 3, pool 3 right behind it). Object offsets and counts are nibble varints (3 bits
// per nibble, low nibble first, bit 3 = continue). Objects are {u32 type, u32 refs, fields}. Decoded here:
// igImage2 (format 1 = CMPR, 3 = RGB565, 4 = I8, with mip chain) -> texNNNN.png, and igGeometryAttr with its
// igVertexBuffer + igIndexBuffer (GX style indexed vertices, strips) -> model.glb. The geometry's texture is the one
// of the first igTextureBindAttr2 following the igGeometry.

typedef struct
{
	const u8 *d;
	size_t sz;
	u32 sec_off[4], sec_sz[4];
	char (*types)[48];
	u32 ntypes;
	u32 *tsize;
	u32 *objs, nobjs;
	u32 *mpos, nmpos;
	const u8 *mblk; // pairs {size, pool<<24|offset}
} igz_t;

// Decodes up to 'count' nibble varints of [p, p+len). Returns the number decoded.
static u32 igz_nibbles (const u8 *p, size_t len, u32 count, u32 *out)
{
	u32 n = 0, acc = 0, shift = 0;
	for (size_t i = 0; i < len * 2 && n < count; i++)
	{
		const uint nib = (i & 1) ? p[i / 2] >> 4 : p[i / 2] & 15;
		acc |= (u32)(nib & 7) << shift;
		shift += 3;
		if (!(nib & 8))
		{
			out[n++] = acc;
			acc = shift = 0;
		}
		else if (shift > 30)
			break;
	}
	return n;
}

static void igz_free (igz_t *z)
{
	FREE (z->types);
	FREE (z->tsize);
	FREE (z->objs);
	FREE (z->mpos);
}

static bool igz_cumulative (const u8 *p, size_t len, u32 count, u32 **out, u32 *nout)
{
	u32 *v = MALLOC ((size_t)count * sizeof (*v) + 4);
	if (!v)
		return false;
	const u32 n = igz_nibbles (p, len, count, v);
	u32 o = 0;
	for (u32 i = 0; i < n; i++)
	{
		o += 4 * (v[i] + 1);
		v[i] = o;
	}
	*out = v;
	*nout = n;
	return true;
}

static bool igz_parse (igz_t *z, const u8 *d, size_t sz)
{
	memset (z, 0, sizeof (*z));
	z->d = d;
	z->sz = sz;
	if (sz < 0x50 || rd_be32 (d) != 0x49475a01)
		return false;
	for (uint i = 0; i < 4; i++)
	{
		z->sec_off[i] = rd_be32 (d + 0x10 + 16 * i);
		z->sec_sz[i] = rd_be32 (d + 0x14 + 16 * i);
		if (z->sec_off[i] > sz || z->sec_sz[i] > sz - z->sec_off[i])
			return false;
	}
	const size_t f = z->sec_off[0];
	if (z->sec_sz[0] < 0x20)
		return false;
	const u32 first = rd_be32 (d + f + 12), nrec = rd_be32 (d + f + 16);
	size_t p = f + first;
	for (u32 i = 0; i < nrec && p + 24 <= f + z->sec_sz[0]; i++)
	{
		const u32 tag = rd_be32 (d + p), count = rd_be32 (d + p + 12), len = rd_be32 (d + p + 16),
			  start = rd_be32 (d + p + 20);
		if (len < 24 || start > len || p + len > sz || count > 0x1000000)
			return false;
		if (tag == 0)
		{
			z->types = CALLOC (count + 1, sizeof (*z->types));
			if (!z->types)
				return false;
			size_t q = p + 24;
			for (u32 t = 0; t < count && q < p + len; t++)
			{
				snprintf (z->types[t], sizeof (*z->types), "%s", (ccp)d + q);
				q += strnlen ((ccp)d + q, p + len - q) + 1;
				z->ntypes++;
			}
		}
		else if (tag == 13 && 24 + 4 * (size_t)count <= len)
		{
			z->tsize = CALLOC (count + 1, sizeof (*z->tsize));
			if (!z->tsize)
				return false;
			for (u32 t = 0; t < count; t++)
				z->tsize[t] = rd_be32 (d + p + 24 + 4 * t);
		}
		else if (tag == 5)
		{
			if (!igz_cumulative (d + p + start, len - start, count, &z->objs, &z->nobjs))
				return false;
		}
		else if (tag == 12)
		{
			if (!igz_cumulative (d + p + start, len - start, count, &z->mpos, &z->nmpos))
				return false;
		}
		else if (tag == 11 && start + 8 * (size_t)count <= len)
		{
			z->mblk = d + p + start;
			if (!z->nmpos)
				z->nmpos = count; // overwritten by tag 12 (which precedes 11)
		}
		p += len;
	}
	return z->types && z->tsize && z->objs && z->mpos && z->mblk;
}

static const u8 *igz_obj (const igz_t *z, u32 pos, uint nwords)
{
	if (pos > z->sec_sz[1] || z->sec_sz[1] - pos < 4 * (size_t)nwords)
		return 0;
	return z->d + z->sec_off[1] + pos;
}

static int igz_type (const igz_t *z, ccp name)
{
	for (u32 i = 0; i < z->ntypes; i++)
		if (!strcmp (z->types[i], name))
			return (int)i;
	return -1;
}

// Memory block referenced by the field at 'fpos'; returns its data and size.
static const u8 *igz_block (const igz_t *z, u32 fpos, size_t *size)
{
	u32 lo = 0, hi = z->nmpos;
	while (lo < hi)
	{
		const u32 mid = (lo + hi) / 2;
		if (z->mpos[mid] < fpos)
			lo = mid + 1;
		else
			hi = mid;
	}
	if (lo >= z->nmpos || z->mpos[lo] != fpos)
		return 0;
	const u32 bsize = rd_be32 (z->mblk + 8 * lo) & 0xffffff, loc = rd_be32 (z->mblk + 8 * lo + 4);
	const uint pool = loc >> 24;
	if (pool != 2 && pool != 3)
		return 0;
	const u64 base = (u64)z->sec_off[3] + (pool == 3 ? z->sec_sz[3] : 0), at = base + (loc & 0xffffff);
	if (at + bsize > z->sz)
		return 0;
	*size = bsize;
	return z->d + at;
}

// Object type name at 'pos' (or NULL when out of range).
static ccp igz_type_at (const igz_t *z, u32 pos)
{
	const u8 *o = igz_obj (z, pos, 2);
	if (!o)
		return 0;
	const u32 t = rd_be32 (o);
	return t < z->ntypes ? z->types[t] : 0;
}

typedef struct
{
	u32 w, h, mips, fmt, pos;
	bool ok;
} igz_img_t;

// Builds one mesh from a geometry attribute (vertex buffer + index buffer). Returns false when it is not usable.
static bool igz_mesh (const igz_t *z, u32 vbpos, u32 ibpos, int material, model_t *model, uint index)
{
	const u8 *vb = igz_obj (z, vbpos, 8), *ib = igz_obj (z, ibpos, 9);
	if (!vb || !ib)
		return false;
	const u32 nv = rd_be32 (vb + 8), vfpos = rd_be32 (vb + 24), icount = rd_be32 (ib + 8);
	const u8 *vf = igz_obj (z, vfpos, 8);
	size_t vsz = 0, isz = 0;
	const u8 *vdata = igz_block (z, vbpos + 20, &vsz), *idata = igz_block (z, ibpos + 20, &isz);
	if (!vf || !vdata || !idata || !nv || !icount || nv > 0x100000 || icount > 0x1000000)
		return false;
	const u32 stride = rd_be32 (vf + 8), bpos = rd_be32 (vf + 24), bsize = rd_be32 (vf + 20) & 0xffffff;
	const u8 *bl = igz_obj (z, bpos, bsize / 4);
	if (!bl || stride < 12 || 4 + (u64)nv * stride > vsz)
		return false;
	// element list {id, flag, type, junk}: 9 position, 0xa normal (s16 x3), 0xb color, 0xd..0xf texcoord (s16 x2)
	uint off = 0, nid = 0, pos_off = ~0u, col_off = ~0u, uv_off = ~0u;
	for (uint e = 0; e * 16 + 16 <= bsize; e++)
	{
		const u32 id = rd_be32 (bl + 16 * e);
		if (id == 0xff)
			break;
		nid++;
		if (id == 9)
			pos_off = off, off += 12;
		else if (id == 0xa)
			off += 6;
		else if (id == 0xb)
			col_off = col_off == ~0u ? off : col_off, off += 4;
		else if (id == 0xd || id == 0xe || id == 0xf)
			uv_off = uv_off == ~0u ? off : uv_off, off += 4;
		else
			return false;
	}
	if (pos_off == ~0u || off > stride)
		return false;
	const bool u16 = (rd_be32 (ib + 24) & 2) != 0;
	const uint isize = u16 ? 2 : 1;
	// bytes per index element: one index per vertex attribute, the block is padded to 0x20
	size_t es = (size_t)nid * isize;
	if (((4 + (size_t)icount * es + 31) & ~(size_t)31) != isz)
	{
		es = (isz - 4) / icount;
		if (es < isize)
			return false;
	}
	if (4 + (size_t)icount * es > isz)
		return false;

	vec3_t *pos = MALLOC (nv * sizeof (*pos) + 1);
	vec2_t *uv = uv_off != ~0u ? MALLOC (nv * sizeof (*uv) + 1) : 0;
	color4_t *col = col_off != ~0u ? MALLOC (nv * sizeof (*col) + 1) : 0;
	u32 *idx = MALLOC ((size_t)icount * sizeof (*idx) + 4);
	if (!pos || !idx)
	{
		FREE (pos);
		FREE (uv);
		FREE (col);
		FREE (idx);
		return false;
	}
	for (u32 i = 0; i < nv; i++)
	{
		const u8 *v = vdata + 4 + (size_t)i * stride;
		union
		{
			u32 u;
			float f;
		} x, y, w;
		x.u = rd_be32 (v + pos_off);
		y.u = rd_be32 (v + pos_off + 4);
		w.u = rd_be32 (v + pos_off + 8);
		pos[i].x = x.f;
		pos[i].y = y.f;
		pos[i].z = w.f;
		if (uv)
		{
			uv[i].u = (float)(s16)rd_be16 (v + uv_off) / 16384.0f;
			uv[i].v = (float)(s16)rd_be16 (v + uv_off + 2) / 16384.0f;
		}
		if (col)
		{
			col[i].r = v[col_off] / 255.0f;
			col[i].g = v[col_off + 1] / 255.0f;
			col[i].b = v[col_off + 2] / 255.0f;
			col[i].a = v[col_off + 3] / 255.0f;
		}
	}
	for (u32 i = 0; i < icount; i++)
		idx[i] = u16 ? rd_be16 (idata + 4 + (size_t)i * es) : idata[4 + (size_t)i * es];

	// triangle strips stitched with degenerate triangles
	const bool strip = true;
	size_t ntri = icount >= 2 ? icount - 2 : 0;
	vertex_t *verts = CALLOC (ntri * 3 + 3, sizeof (*verts));
	int *tri_mat = CALLOC (ntri + 1, sizeof (*tri_mat));
	mesh_t *meshes = REALLOC (model->meshes, (model->num_meshes + 1) * sizeof (*meshes));
	if (!verts || !tri_mat || !meshes)
	{
		FREE (pos);
		FREE (uv);
		FREE (col);
		FREE (idx);
		FREE (verts);
		FREE (tri_mat);
		if (meshes)
			model->meshes = meshes;
		return false;
	}
	size_t nout = 0;
	float pnx = 0, pny = 0, pnz = 0;
	bool have_prev = false;
	for (size_t t = 0; t < ntri; t++)
	{
		u32 a, b, c;
		if (strip)
		{
			a = idx[t];
			b = idx[t + 1 + (t & 1)];
			c = idx[t + 2 - (t & 1)];
		}
		else
		{
			a = idx[3 * t];
			b = idx[3 * t + 1];
			c = idx[3 * t + 2];
		}
		if (a >= nv || b >= nv || c >= nv || a == b || b == c || a == c)
			continue;
		if (strip)
		{
			// stitched strips shift the parity: keep each triangle facing like its predecessor
			const vec3_t *A = pos + a, *B = pos + b, *C = pos + c;
			const float ux = B->x - A->x, uy = B->y - A->y, uz = B->z - A->z, vx = C->x - A->x, vy = C->y - A->y,
				    vz = C->z - A->z;
			const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
			if (have_prev && nx * pnx + ny * pny + nz * pnz < 0)
			{
				const u32 tmp = b;
				b = c;
				c = tmp;
				pnx = -nx;
				pny = -ny;
				pnz = -nz;
			}
			else
			{
				pnx = nx;
				pny = ny;
				pnz = nz;
			}
			have_prev = true;
		}
		const u32 tri[3] = {a, b, c};
		for (uint k = 0; k < 3; k++)
		{
			vertex_t *vt = verts + nout * 3 + k;
			vt->position_idx = tri[k];
			vt->normal_idx = vt->tangent_idx = vt->matrix_idx = -1;
			vt->texcoord_idx = uv ? (int)tri[k] : -1;
			vt->color_idx[0] = col ? (int)tri[k] : -1;
			vt->color_idx[1] = -1;
			for (int e = 0; e < 7; e++)
				vt->extra_texcoord_idx[e] = -1;
		}
		tri_mat[nout++] = material;
	}
	FREE (idx);
	model->meshes = meshes;
	mesh_t *mesh = meshes + model->num_meshes++;
	memset (mesh, 0, sizeof (*mesh));
	snprintf (mesh->name, sizeof (mesh->name), "mesh%u", index);
	mesh->positions = pos;
	mesh->num_positions = nv;
	mesh->texcoords = uv;
	mesh->num_texcoords = uv ? nv : 0;
	mesh->colors[0] = col;
	mesh->num_colors[0] = col ? nv : 0;
	mesh->vertices = verts;
	mesh->num_vertices = nout * 3;
	mesh->triangle_materials = tri_mat;
	mesh->material_idx = material;
	return true;
}

static enumError extract_igz_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!(export_count > 0 || opt_decode) || !petz_has_magic (arg, "IGZ\x01"))
		return ERR_NOTHING_TO_DO;
	{
		u8 *raw = 0;
		size_t rsz = 0;
		if (LoadFileAlloc (arg, 0, 0, &raw, &rsz, 0, 0, 0, false) || !raw)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		igz_t z;
		if (!igz_parse (&z, raw, rsz))
		{
			igz_free (&z);
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}

		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const int t_img = igz_type (&z, "igImage2"), t_geo = igz_type (&z, "igGeometry"),
			  t_ga = igz_type (&z, "igGeometryAttr"), t_bind = igz_type (&z, "igTextureBindAttr2");
		igz_img_t *imgs = CALLOC (z.nobjs + 1, sizeof (*imgs));
		uint nimg = 0, npng = 0;
		model_t *model = CALLOC (1, sizeof (*model));
		if (!imgs || !model || t_img < 0 || t_geo < 0 || t_ga < 0)
		{
			FREE (imgs);
			FreeModel (model);
			igz_free (&z);
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}

		// textures
		for (u32 i = 0; i < z.nobjs; i++)
		{
			const u8 *o = igz_obj (&z, z.objs[i], 13);
			if (!o || rd_be32 (o) != (u32)t_img)
				continue;
			igz_img_t *im = imgs + nimg++;
			im->pos = z.objs[i];
			im->h = rd_be32 (o + 12) >> 16;
			im->w = rd_be32 (o + 12) & 0xffff;
			im->mips = rd_be32 (o + 16) & 0xffff;
			im->fmt = rd_be32 (o + 24) & 0xffff;
			size_t bsz = 0;
			const u8 *px = igz_block (&z, im->pos + 0x24, &bsz);
			const int gx = im->fmt == 1 ? 14 : im->fmt == 3 ? 4 : im->fmt == 4 ? 1 : -1;
			if (!px || gx < 0 || !im->w || !im->h || im->w > 4096 || im->h > 4096)
				continue;
			u8 *rgba = 0;
			if (DecodeGXTexture_RGBA (&rgba, im->w, im->h, gx, px, (uint)bsz, 0, 0, 0) || !rgba)
				continue;
			im->ok = true;
			char png[PATH_MAX];
			snprintf (png, sizeof (png), "%s/%stex%04u.png", dest, basedir ? basedir : "", nimg - 1);
			if (testmode)
				FREE (rgba);
			else
			{
				SaveDecodedRGBAToPNG (rgba, im->w, im->h, &be_func, png, 0, true);
				npng++;
			}
		}

		// materials: one per image
		model->materials = CALLOC (nimg + 1, sizeof (*model->materials));
		model->num_materials = model->materials ? nimg + 1 : 0;
		if (model->materials)
			for (uint i = 0; i <= nimg; i++)
			{
				material_t *m = model->materials + i;
				if (i < nimg && imgs[i].ok)
				{
					snprintf (m->name, sizeof (m->name), "tex%04u", i);
					snprintf (m->textures[0], sizeof (m->textures[0]), "tex%04u.png", i);
					m->num_textures = 1;
				}
				else
					snprintf (m->name, sizeof (m->name), "untextured");
				m->diffuse[0] = m->diffuse[1] = m->diffuse[2] = m->diffuse[3] = 1.0f;
			}

		// meshes: geometry attributes between two igGeometry objects share the texture of the first bind attribute
		u32 *gas = MALLOC ((z.nobjs + 1) * sizeof (*gas));
		uint nga = 0, nmesh = 0;
		int cur_tex = -1;
		for (u32 i = 0; i <= z.nobjs && gas; i++)
		{
			const u8 *o = i < z.nobjs ? igz_obj (&z, z.objs[i], 2) : 0;
			const u32 t = o ? rd_be32 (o) : ~0u;
			if (i == z.nobjs || t == (u32)t_geo)
			{
				for (uint g = 0; g < nga; g++)
				{
					const u8 *ga = igz_obj (&z, gas[g], 11);
					if (!ga)
						continue;
					const int mat = cur_tex >= 0 && model->materials && cur_tex < (int)nimg && imgs[cur_tex].ok
						? cur_tex
						: (int)nimg;
					if (igz_mesh (&z, rd_be32 (ga + 16), rd_be32 (ga + 20), mat, model, nmesh))
						nmesh++;
				}
				nga = 0;
				cur_tex = -1;
			}
			else if (t == (u32)t_ga)
			{
				if (igz_obj (&z, z.objs[i], 11))
					gas[nga++] = z.objs[i];
			}
			else if (t_bind >= 0 && t == (u32)t_bind && cur_tex < 0)
			{
				// bind -> igTextureAttr2 (field 11) -> igImage2
				const u8 *ta = igz_obj (&z, rd_be32 (o + 16), 12);
				const u32 ip = ta ? rd_be32 (ta + 44) : 0;
				for (uint k = 0; k < nimg && ip; k++)
					if (imgs[k].pos == ip)
					{
						cur_tex = (int)k;
						break;
					}
			}
		}
		FREE (gas);

		enumError err = ERR_NOTHING_TO_DO;
		if (nmesh || npng)
		{
			if (nmesh && !testmode)
			{
				char glb[PATH_MAX];
				snprintf (glb, sizeof (glb), "%s/%smodel.glb", dest, basedir ? basedir : "");
				ExportModelToGLB (model, glb);
			}
			if (verbose >= 0)
				fprintf (stdlog, "%s%sEXTRACT IGZ:%s -> %u PNG, %u meshes\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", arg, npng, nmesh);
			err = ERR_OK;
		}
		FreeModel (model);
		FREE (imgs);
		igz_free (&z);
		FREE (raw);
		return err;
	}
}
