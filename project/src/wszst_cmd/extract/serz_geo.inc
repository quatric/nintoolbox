// Ubisoft/Kuju SERZ geometry (".GeoWii": Sleepover Party; same token stream as ".TgWii", see serz_tg.inc, plus {0xff 'R', name, u32} and {0xff 'A', name, type, u8 count, values}
// tokens; 'p' tokens and 'R' tokens also take an entity number, entities are 0-based in compact references, and a compact 'p' carries no payload). One cHcPrimitiveSetDescWii
// holds a vertex-array blob (big endian: array 0 positions as 3 x float, array 1 normals as 3 x float when its size equals the positions' (else it is the colour array, 4 bytes
// per entry; VF_POSCOLTEX has a colour array after the optional normals), then texture coordinates as 2 x float; further arrays are not used) at ArrayOffset/ArraySize, and a GX display list blob (0x80 quads / 0x90 triangles / 0x98 strip / 0xA0 fan,
// u16 vertex count, then per vertex one big-endian u16 index per array column: 3, 4 or 5 columns). Written as "<name>.obj" (v/vt/vn, triangles; skinning ignored).
static float serz_be_float (const u8 *p)
{
	const uint v = rd_be32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

typedef struct serz_geo_set_t
{
	int offs[16], sizes[16];
	uint noff, nsize;
	const u8 *blob[2];
	size_t blen[2];
	uint nblob;
	bool posnorm;
} serz_geo_set_t;

// Appends one primitive set to the OBJ text; returns the number of triangles written (0: unusable). *nv / *nt / *nn count the vertices written so far.
static uint serz_geo_emit (const serz_geo_set_t *s, char *txt, size_t *o, size_t cap, uint *nvb, uint *ntb, uint *nnb, uint index)
{
	if (s->nblob != 2 || s->noff != s->nsize || s->noff < 3 || s->noff > 16 || s->offs[0] < 0 || s->sizes[0] <= 0 || s->sizes[0] % 12)
		return 0;
	const u8 *vd = s->blob[0], *dl = s->blob[1];
	const size_t vl = s->blen[0], dll = s->blen[1];
	for (uint i = 0; i < s->noff; i++)
		if (s->offs[i] < 0 || s->sizes[i] < 0 || (size_t)s->offs[i] + (size_t)s->sizes[i] > vl)
			return 0;
	uint stride = 0;
	// the vertex size in the display list is the one that tiles it exactly
	for (uint st = 6; !stride && st <= 20; st += 2)
	{
		size_t q = 0;
		bool good = true;
		while (q < dll && good)
		{
			const u8 c = dl[q];
			if (!c)
			{
				for (size_t z = q; z < dll; z++)
					if (dl[z])
						good = false;
				break;
			}
			if (c != 0x80 && c != 0x90 && c != 0x98 && c != 0xa0 && c != 0xa8 && c != 0xb0)
				good = false;
			else if (q + 3 > dll)
				good = false;
			else
				q += 3 + (size_t)rd_be16 (dl + q + 1) * st;
			if (q > dll)
				good = false;
		}
		if (good)
			stride = st;
	}
	const uint cols = stride / 2;
	if (!stride || cols < 3 || cols > s->noff)
		return 0;
	// highest index used per column; normals are the second array when its elements are at least 12 bytes
	uint mx[16] = { 0 };
	for (size_t q = 0; q + 3 <= dll && dl[q];)
	{
		const uint cnt = rd_be16 (dl + q + 1);
		for (uint i = 0; i < cnt; i++)
			for (uint c = 0; c < cols; c++)
			{
				const uint v = rd_be16 (dl + q + 3 + (size_t)i * stride + 2 * c);
				if (v > mx[c])
					mx[c] = v;
			}
		q += 3 + (size_t)cnt * stride;
	}
	const bool hasnrm = s->sizes[1] % 12 == 0 && (uint)s->sizes[1] / (mx[1] + 1) >= 12;
	const uint uvcol = 1 + (hasnrm ? 1 : 0) + (s->posnorm ? 0 : 1);
	if (uvcol >= cols || s->sizes[uvcol] <= 0 || s->sizes[uvcol] % 8)
		return 0;
	const uint npos = (uint)s->sizes[0] / 12, ntex = (uint)s->sizes[uvcol] / 8, nnrm = hasnrm ? (uint)s->sizes[1] / 12 : 0;
	if (mx[0] >= npos || mx[uvcol] >= ntex || (nnrm && mx[1] >= nnrm) || *o + (size_t)(npos + ntex + nnrm) * 48 + dll * 24 + 64 > cap)
		return 0;
	*o += (size_t)sprintf (txt + *o, "g set%u\n", index);
	for (uint i = 0; i < npos; i++)
		*o += (size_t)sprintf (txt + *o, "v %.6f %.6f %.6f\n", serz_be_float (vd + s->offs[0] + 12 * i), serz_be_float (vd + s->offs[0] + 12 * i + 4), serz_be_float (vd + s->offs[0] + 12 * i + 8));
	for (uint i = 0; i < ntex; i++)
		*o += (size_t)sprintf (txt + *o, "vt %.6f %.6f\n", serz_be_float (vd + s->offs[uvcol] + 8 * i), 1.0f - serz_be_float (vd + s->offs[uvcol] + 8 * i + 4));
	for (uint i = 0; i < nnrm; i++)
		*o += (size_t)sprintf (txt + *o, "vn %.6f %.6f %.6f\n", serz_be_float (vd + s->offs[1] + 12 * i), serz_be_float (vd + s->offs[1] + 12 * i + 4), serz_be_float (vd + s->offs[1] + 12 * i + 8));
	uint ntri = 0;
	for (size_t q = 0; q + 3 <= dll && dl[q];)
	{
		const u8 c = dl[q];
		const uint cnt = rd_be16 (dl + q + 1);
		const u8 *v = dl + q + 3;
		q += 3 + (size_t)cnt * stride;
		for (uint i = 0; i < cnt; i++)
		{
			uint idx[3] = { 0, 0, 0 };
			bool emit = false, flip = false;
			if (c == 0x90 && i % 3 == 2)
				idx[0] = i - 2, idx[1] = i - 1, idx[2] = i, emit = true;
			else if (c == 0x98 && i >= 2)
				idx[0] = i - 2, idx[1] = i - 1, idx[2] = i, emit = true, flip = (i & 1) != 0;
			else if (c == 0xa0 && i >= 2)
				idx[0] = 0, idx[1] = i - 1, idx[2] = i, emit = true;
			for (uint h = 0; h < (c == 0x80 && i % 4 == 3 ? 2u : emit ? 1u : 0u); h++)
			{
				uint ii[3] = { idx[0], idx[1], idx[2] };
				if (c == 0x80)
				{
					ii[0] = i - 3;
					ii[1] = i - 3 + 1 + h;
					ii[2] = i - 3 + 2 + h;
				}
				else if (flip)
				{
					ii[1] = idx[2];
					ii[2] = idx[1];
				}
				uint pv[3], tv[3], nv[3];
				for (uint k = 0; k < 3; k++)
				{
					pv[k] = rd_be16 (v + stride * ii[k]);
					nv[k] = nnrm ? rd_be16 (v + stride * ii[k] + 2) + 1 + *nnb : 0;
					tv[k] = rd_be16 (v + stride * ii[k] + 2 * uvcol) + 1 + *ntb;
					pv[k] += 1;
				}
				if (pv[0] != pv[1] && pv[1] != pv[2] && pv[0] != pv[2] && *o + 160 < cap)
				{
					pv[0] += *nvb, pv[1] += *nvb, pv[2] += *nvb;
					*o += (size_t)sprintf (txt + *o, nnrm ? "f %u/%u/%u %u/%u/%u %u/%u/%u\n" : "f %u/%u %u/%u %u/%u\n", pv[0], tv[0], nv[0], pv[1], tv[1], nv[1], pv[2], tv[2], nv[2]);
					ntri++;
				}
			}
		}
	}
	*nvb += npos;
	*ntb += ntex;
	*nnb += nnrm;
	return ntri;
}

static enumError extract_serz_geo_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".GeoWii") || !file_has_magic (arg, "SERZ", 4))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	serz_str_t *tab = CALLOC (8192, sizeof (serz_str_t));
	serz_ent_t *ent = CALLOC (8192, sizeof (serz_ent_t));
	serz_geo_set_t *sets = CALLOC (64, sizeof (serz_geo_set_t));
	uint ntab = 0, nent = 0, nsets = 0;
	int want = 0, mode = 0;
	bool fail = false;
	size_t p = 8;
	while (tab && ent && sets && !fail && p + 2 <= sz)
	{
		const u8 b = raw[p];
		serz_str_t n = { 0, 0 }, t = { 0, 0 };
		char kind = 0;
		if (b == 0xff)
		{
			const u8 k = raw[p + 1];
			p += 2;
			if (k == 'C')
			{
				p += 5;
				continue;
			}
			if (k == 'N')
				continue;
			if (k == 'P' || k == 'V' || k == 'R' || k == 'A')
			{
				if (!serz_ref (raw, sz, &p, tab, &ntab, &n) || nent >= 8192)
				{
					fail = true;
					break;
				}
				if ((k == 'V' || k == 'A') && !serz_ref (raw, sz, &p, tab, &ntab, &t))
				{
					fail = true;
					break;
				}
				ent[nent].name = n;
				ent[nent].type = t;
				ent[nent].kind = kind = (char)k;
				nent++;
			}
			else if (k == 'B')
			{
				if (nent >= 8192)
				{
					fail = true;
					break;
				}
				ent[nent++].kind = kind = 'B';
			}
			else if (k == 'p')
			{
				if (nent >= 8192)
				{
					fail = true;
					break;
				}
				ent[nent++].kind = 'p';
				p += 2;
				continue;
			}
			else
			{
				fail = true;
				break;
			}
		}
		else
		{
			p++;
			if (b >= nent)
			{
				fail = true;
				break;
			}
			kind = ent[b].kind;
			n = ent[b].name;
			t = ent[b].type;
			if (kind == 'p')
				continue;
		}
		serz_geo_set_t *cur = nsets ? &sets[nsets - 1] : 0;
		if (kind == 'P')
		{
			if (p + 8 > sz)
			{
				fail = true;
				break;
			}
			const uint cnt = rd_le32 (raw + p + 4);
			p += 8;
			mode = 0;
			if (serz_is (n, "cHcPrimitiveSetDescWii") && nsets < 64)
				nsets++;
			else if (serz_is (n, "ArrayOffset"))
				mode = 1, want = (int)cnt, cur ? cur->noff = 0 : 0;
			else if (serz_is (n, "ArraySize"))
				mode = 2, want = (int)cnt, cur ? cur->nsize = 0 : 0;
		}
		else if (kind == 'R')
			p += 4;
		else if (kind == 'B')
		{
			if (p + 4 > sz)
			{
				fail = true;
				break;
			}
			const size_t l = rd_le32 (raw + p);
			if (p + 4 + l > sz)
			{
				fail = true;
				break;
			}
			if (cur && cur->nblob < 2)
			{
				cur->blob[cur->nblob] = raw + p + 4;
				cur->blen[cur->nblob++] = l;
			}
			p += 4 + l;
		}
		else if (kind == 'A')
		{
			if (p >= sz)
			{
				fail = true;
				break;
			}
			const uint cnt = raw[p++], vs = serz_vsize (t);
			for (uint i = 0; i < cnt && !fail; i++)
			{
				if (vs)
				{
					if (p + vs > sz)
						fail = true;
					p += vs;
				}
				else
				{
					serz_str_t sv;
					fail = !serz_ref (raw, sz, &p, tab, &ntab, &sv);
				}
			}
		}
		else if (kind == 'V')
		{
			const uint vs = serz_vsize (t);
			serz_str_t sv = { 0, 0 };
			uint val = 0;
			if (vs)
			{
				if (p + vs > sz)
				{
					fail = true;
					break;
				}
				val = vs >= 4 ? rd_le32 (raw + p) : vs == 2 ? rd_le16 (raw + p) : raw[p];
				p += vs;
			}
			else if (!serz_ref (raw, sz, &p, tab, &ntab, &sv))
			{
				fail = true;
				break;
			}
			if (cur && serz_is (n, "e") && serz_is (t, "sInt32") && want > 0)
			{
				if (mode == 1 && cur->noff < 16)
					cur->offs[cur->noff++] = (int)val;
				else if (mode == 2 && cur->nsize < 16)
					cur->sizes[cur->nsize++] = (int)val;
				want--;
			}
			else if (serz_is (n, "PreferredVertexCreationFormat") && vs == 0)
			{
				// the shader technique precedes its geometry's primitive sets only loosely; keep the latest value for every set that follows
				for (uint i = 0; i < 64; i++)
					if (i >= nsets)
						sets[i].posnorm = serz_is (sv, "VF_POSNORMTEX");
			}
		}
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (!fail && nsets)
	{
		size_t cap = 1 << 20;
		char *txt = CALLOC (cap, 1);
		bool grow = true;
		while (txt && grow)
		{
			grow = false;
			size_t o = (size_t)sprintf (txt, "# SERZ geometry (skinning ignored)\n");
			uint nvb = 0, ntb = 0, nnb = 0, ntri = 0;
			for (uint i = 0; i < nsets; i++)
				ntri += serz_geo_emit (&sets[i], txt, &o, cap, &nvb, &ntb, &nnb, i);
			if (o + 256 >= cap || !ntri)
			{
				if (ntri && cap < ((size_t)1 << 28))
				{
					FREE (txt);
					cap *= 4;
					txt = CALLOC (cap, 1);
					grow = true;
				}
				continue;
			}
			char dest[PATH_MAX], name[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (name, '.');
			if (dot) strcpy (dot, ".obj");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT SERZ-GEO:%s (%u sets, %u triangles) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nsets, ntri, dest, name);
			err = shade_write (arg, dest, basedir, name, (u8 *)txt, (uint)o);
		}
		FREE (txt);
	}
	FREE (tab);
	FREE (ent);
	FREE (sets);
	FREE (raw);
	return err;
}
