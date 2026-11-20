// Call of Duty: World at War (Wii) geometry streams (".xgm" static models, ".sgm" BSP/static surface groups; big endian). The file is a raw in-memory image: u16 magic 0x4321, u16
// version 1, u32 fixup count N, then N {u32 location, u32 target} pairs (the engine writes base + target into the word at base + location; unpatched pointers read 0xffbaabff). The
// vertex arrays are described by 0x20 byte declarations {u16 1, u16 element count, 0, u8 stride, u8 1, u16 GQR style word (low 3 bits type: 7 s16, 6 s8, 5 u16, 4 u8, 0 f32; bits 8..13
// fraction bits), ..., name pointer at +0x18 ("Position", "Normal", "TexCoord", "Color", "Lightmap"), data pointer at +0x1c}. A draw is {u32 attribute count, u32 attribute table
// size, attribute table pointer, 0, display list pointer, u32 display list size} where the table has 8 byte entries {u8 GX attribute (9 position, 10 normal, 11 colour, 13+ texcoords),
// 0, 0, u8 stride, array pointer}, and the display list is GX draw commands {u8 command (0x80 quads, 0x90 triangles, 0x98 strip, 0xa0 fan), u16 vertex count, count * vertices} whose
// vertices carry one array index per attribute in table order (u8, or u16 when the array has more than 256 elements). Positions are s16 (stride 6) or f32 (stride 12), normals s16
// or s8, texcoords s16 or f32. Written as "<file>.d/model.obj" (one object per draw with positions, UVs and normals).
typedef struct cg_fix_t { u32 loc, tg; } cg_fix_t;
typedef struct cg_decl_t { u32 data, count; uint stride, gqr; } cg_decl_t;

static int cg_fix_cmp (const void *a, const void *b)
{
	const u32 x = ((const cg_fix_t *)a)->loc, y = ((const cg_fix_t *)b)->loc;
	return x < y ? -1 : x > y;
}

static bool cg_find (const cg_fix_t *f, uint n, u32 loc, u32 *tg)
{
	uint lo = 0, hi = n;
	while (lo < hi)
	{
		const uint mid = (lo + hi) / 2;
		if (f[mid].loc < loc)
			lo = mid + 1;
		else
			hi = mid;
	}
	if (lo < n && f[lo].loc == loc)
	{
		*tg = f[lo].tg;
		return true;
	}
	return false;
}

static const cg_decl_t *cg_decl (const cg_decl_t *dc, uint n, u32 data)
{
	for (uint i = 0; i < n; i++)
		if (dc[i].data == data)
			return dc + i;
	return 0;
}

static float cg_f32 (const u8 *p)
{
	union
	{
		u32 u;
		float f;
	} x = { rd_be32 (p) };
	return x.f;
}

static double cg_comp (const u8 *p, uint type, uint frac)
{
	const double sc = 1.0 / (double)(1u << (frac & 31));
	switch (type)
	{
		case 7: return (s16)rd_be16 (p) * sc;
		case 6: return (s8)p[0] * sc;
		case 5: return rd_be16 (p) * sc;
		case 4: return p[0] * sc;
	}
	return 0;
}

static enumError extract_cod_geom_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if ((!is_ext (arg, ".xgm") && !is_ext (arg, ".sgm")) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *d = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &d, &sz, 0, 0, 0, false) || !d || sz < 0x40 || sz > 0x7fffffff || rd_be16 (d) != 0x4321 || rd_be16 (d + 2) != 1)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	const u32 nf = rd_be32 (d + 4);
	if (!nf || nf > 4000000 || 8 + 8ull * nf > sz)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	cg_fix_t *fx = MALLOC (nf * sizeof (*fx));
	cg_decl_t *dc = MALLOC ((size_t)nf * sizeof (*dc));
	for (u32 i = 0; fx && i < nf; i++)
	{
		fx[i].loc = rd_be32 (d + 8 + 8 * i);
		fx[i].tg = rd_be32 (d + 12 + 8 * i);
	}
	if (!fx || !dc)
	{
		FREE (fx);
		FREE (dc);
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	qsort (fx, nf, sizeof (*fx), cg_fix_cmp);
	// array declarations
	uint ndc = 0;
	for (u32 i = 0; i < nf; i++)
	{
		u32 dt;
		const u32 loc = fx[i].loc, tg = fx[i].tg;
		if (loc < 0x18 || (u64)loc + 8 > sz || tg + 4 > sz || !cg_find (fx, nf, loc + 4, &dt) || (u64)loc - 0x18 + 0x20 > sz)
			continue;
		uint len = 0;
		while (tg + len < sz && len < 16 && d[tg + len] && isalnum (d[tg + len]))
			len++;
		if (len < 3 || len > 12 || tg + len >= sz || d[tg + len])
			continue;
		const u8 *D = d + loc - 0x18;
		if (!rd_be16 (D + 2) || !D[6])
			continue;
		dc[ndc].data = dt;
		dc[ndc].count = rd_be16 (D + 2);
		dc[ndc].stride = D[6];
		dc[ndc].gqr = rd_be16 (D + 8);
		ndc++;
	}
	njm_out_t o = { MALLOC (1 << 20), 0, 1 << 20 };
	uint ndraw = 0, ntri = 0;
	u32 vbase = 1, nbase = 1, tbase = 1;
	for (u32 fi = 0; fi < nf && o.b; fi++)
	{
		u32 atp, dlp, tmp;
		const u32 loc = fx[fi].loc;
		atp = fx[fi].tg;
		if (loc < 8 || (u64)loc + 16 > sz || (u64)atp + 8 > sz || d[atp] != 9 || d[atp + 1] || d[atp + 2] || !cg_find (fx, nf, atp + 4, &tmp) || !cg_find (fx, nf, loc + 8, &dlp))
			continue;
		const u32 na = rd_be32 (d + loc - 8), dls = rd_be32 (d + loc + 12);
		if (na < 1 || na > 8 || (u64)atp + 8ull * na > sz || (u64)dlp + dls > sz || !dls)
			continue;
		uint id[8], stride[8], width[8];
		u32 ptr[8];
		const cg_decl_t *dd[8];
		bool ok = true;
		for (uint k = 0; k < na && ok; k++)
		{
			id[k] = d[atp + 8 * k];
			stride[k] = d[atp + 8 * k + 3];
			ok = cg_find (fx, nf, atp + 8 * k + 4, &ptr[k]) && ptr[k] < sz;
			dd[k] = ok ? cg_decl (dc, ndc, ptr[k]) : 0;
		}
		if (!ok)
			continue;
		// index widths: element count > 256 -> 16 bit, verified by walking the display list to its exact end; otherwise search the combinations
		bool found = false;
		for (uint c = 0; c < (1u << na) + 1 && !found; c++)
		{
			uint vsz = 0;
			for (uint k = 0; k < na; k++)
			{
				if (c == 0)
					width[k] = dd[k] && dd[k]->count > 256 ? 2 : 1;
				else
					width[k] = ((c - 1) >> k & 1) ? 2 : 1;
				vsz += width[k];
			}
			u32 p = dlp;
			const u32 end = dlp + dls;
			bool good = true;
			while (p < end && good)
			{
				const uint op = d[p];
				if (!op)
				{
					p++;
					continue;
				}
				if ((op & 0xf8) != 0x80 && (op & 0xf8) != 0x90 && (op & 0xf8) != 0x98 && (op & 0xf8) != 0xa0)
				{
					good = false;
					break;
				}
				if (p + 3 > end)
				{
					good = false;
					break;
				}
				const u32 n = rd_be16 (d + p + 1);
				if ((u64)p + 3 + (u64)n * vsz > end)
				{
					good = false;
					break;
				}
				p += 3 + n * vsz;
			}
			found = good && p == end;
		}
		uint pi = 99, ni = 99, ti = 99;
		for (uint k = 0; k < na; k++)
		{
			if (id[k] == 9 && pi == 99)
				pi = k;
			if (id[k] == 10 && ni == 99)
				ni = k;
			if (id[k] == 13 && ti == 99)
				ti = k;
		}
		if (!found || pi == 99 || !dd[pi] || (stride[pi] != 6 && stride[pi] != 12))
			continue;
		uint vsz = 0, off[8];
		for (uint k = 0; k < na; k++)
		{
			off[k] = vsz;
			vsz += width[k];
		}
		// pass 1: collect triangles (index triples as vertex numbers inside the display list)
		u32 nv = 0, cap = 0;
		u32 *vi = 0; // per display list vertex: position, normal, texcoord index
		u32 p = dlp;
		const u32 end = dlp + dls;
		u32 *tri = 0, tcap = 0, ntr = 0;
		bool oom = false;
		while (p < end && !oom)
		{
			const uint op = d[p] & 0xf8;
			if (!d[p])
			{
				p++;
				continue;
			}
			const u32 n = rd_be16 (d + p + 1);
			const u32 first = nv;
			for (u32 k = 0; k < n && !oom; k++)
			{
				const u8 *v = d + p + 3 + (size_t)k * vsz;
				if (nv + 1 > cap)
				{
					const u32 nc = cap ? cap * 2 : 1024;
					u32 *nb = MALLOC ((size_t)nc * 3 * sizeof (u32));
					if (!nb)
					{
						oom = true;
						break;
					}
					if (vi)
						memcpy (nb, vi, (size_t)nv * 3 * sizeof (u32));
					FREE (vi);
					vi = nb;
					cap = nc;
				}
				vi[3 * nv] = width[pi] == 2 ? rd_be16 (v + off[pi]) : v[off[pi]];
				vi[3 * nv + 1] = ni != 99 ? (width[ni] == 2 ? rd_be16 (v + off[ni]) : v[off[ni]]) : 0;
				vi[3 * nv + 2] = ti != 99 ? (width[ti] == 2 ? rd_be16 (v + off[ti]) : v[off[ti]]) : 0;
				nv++;
			}
			// triangles over display list vertex numbers
			u32 want = (n >= 3 ? n - 2 : 0) * 2 + 8;
			if (ntr * 3 + want * 3 > tcap)
			{
				u32 nc = tcap ? tcap * 2 : 3072;
				while (nc < ntr * 3 + want * 3)
					nc *= 2;
				u32 *nb = MALLOC ((size_t)nc * sizeof (u32));
				if (!nb)
					oom = true;
				else
				{
					if (tri)
						memcpy (nb, tri, (size_t)ntr * 3 * sizeof (u32));
					FREE (tri);
					tri = nb;
					tcap = nc;
				}
			}
			for (u32 k = 0; !oom && k + 2 < n + (op == 0x80 ? 0 : 0); k++)
			{
				u32 a, b, c;
				if (op == 0x90)
				{
					if (k % 3)
						continue;
					a = first + k;
					b = first + k + 1;
					c = first + k + 2;
				}
				else if (op == 0x98)
				{
					a = first + k + (k & 1);
					b = first + k + 1 - (k & 1);
					c = first + k + 2;
				}
				else if (op == 0xa0)
				{
					a = first;
					b = first + k + 1;
					c = first + k + 2;
				}
				else
				{
					if (k % 4)
						continue;
					// quad: two triangles
					u32 q0 = first + k, q1 = first + k + 1, q2 = first + k + 2;
					if (k + 3 >= n)
						continue;
					const u32 q3 = first + k + 3;
					if (vi[3 * q0] != vi[3 * q1] && vi[3 * q1] != vi[3 * q2] && vi[3 * q0] != vi[3 * q2])
					{
						tri[3 * ntr] = q0;
						tri[3 * ntr + 1] = q1;
						tri[3 * ntr + 2] = q2;
						ntr++;
					}
					a = q0;
					b = q2;
					c = q3;
				}
				if (vi[3 * a] == vi[3 * b] || vi[3 * b] == vi[3 * c] || vi[3 * a] == vi[3 * c])
					continue;
				tri[3 * ntr] = a;
				tri[3 * ntr + 1] = b;
				tri[3 * ntr + 2] = c;
				ntr++;
			}
			p += 3 + n * vsz;
		}
		if (!oom && ntr)
		{
			const cg_decl_t *pd = dd[pi], *nd = ni != 99 ? dd[ni] : 0, *td = ti != 99 ? dd[ti] : 0;
			const u32 pn = pd->count, nn = nd ? nd->count : 0, tn = td ? td->count : 0;
			u8 *pu = CALLOC (pn + 1, 1), *nu = CALLOC (nn + 1, 1), *tu = CALLOC (tn + 1, 1);
			u32 *pm = CALLOC (pn + 1, sizeof (u32)), *nm = CALLOC (nn + 1, sizeof (u32)), *tm = CALLOC (tn + 1, sizeof (u32));
			if (pu && nu && tu && pm && nm && tm)
			{
				for (u32 t = 0; t < ntr * 3; t++)
				{
					const u32 *e = vi + 3 * tri[t];
					if (e[0] < pn)
						pu[e[0]] = 1;
					if (nd && e[1] < nn)
						nu[e[1]] = 1;
					if (td && e[2] < tn)
						tu[e[2]] = 1;
				}
				njm_put (&o, "o draw_%u\n", ndraw);
				const uint pt = pd->gqr & 7, pfr = pd->gqr >> 8 & 63;
				u32 cnt = 0;
				for (u32 i = 0; i < pn && o.b; i++)
					if (pu[i] && (u64)ptr[pi] + (u64)(i + 1) * stride[pi] <= sz)
					{
						const u8 *q = d + ptr[pi] + (size_t)i * stride[pi];
						double x, y, z;
						if (stride[pi] == 12)
						{
							x = cg_f32 (q);
							y = cg_f32 (q + 4);
							z = cg_f32 (q + 8);
						}
						else
						{
							x = cg_comp (q, pt, pfr);
							y = cg_comp (q + 2, pt, pfr);
							z = cg_comp (q + 4, pt, pfr);
						}
						njm_put (&o, "v %g %g %g\n", x, y, z);
						pm[i] = vbase + cnt++;
					}
				u32 ncnt = 0, tcnt = 0;
				for (u32 i = 0; i < nn && nd && o.b; i++)
					if (nu[i] && (u64)ptr[ni] + (u64)(i + 1) * stride[ni] <= sz && (stride[ni] == 6 || stride[ni] == 3))
					{
						const u8 *q = d + ptr[ni] + (size_t)i * stride[ni];
						const uint nt = nd->gqr & 7, nfr = nd->gqr >> 8 & 63, es = stride[ni] / 3;
						double x = cg_comp (q, nt, nfr), y = cg_comp (q + es, nt, nfr), z = cg_comp (q + 2 * es, nt, nfr);
						const double l = sqrt (x * x + y * y + z * z);
						if (l > 0)
						{
							x /= l;
							y /= l;
							z /= l;
						}
						njm_put (&o, "vn %g %g %g\n", x, y, z);
						nm[i] = nbase + ncnt++;
					}
				for (u32 i = 0; i < tn && td && o.b; i++)
					if (tu[i] && (u64)ptr[ti] + (u64)(i + 1) * stride[ti] <= sz && (stride[ti] == 4 || stride[ti] == 8))
					{
						const u8 *q = d + ptr[ti] + (size_t)i * stride[ti];
						const uint tt = td->gqr & 7, tfr = td->gqr >> 8 & 63;
						double s, t;
						if (stride[ti] == 8)
						{
							s = cg_f32 (q);
							t = cg_f32 (q + 4);
						}
						else
						{
							s = cg_comp (q, tt, tfr);
							t = cg_comp (q + 2, tt, tfr);
						}
						njm_put (&o, "vt %g %g\n", s, 1.0 - t);
						tm[i] = tbase + tcnt++;
					}
				for (u32 t = 0; t < ntr && o.b; t++)
				{
					char line[200];
					int ln = snprintf (line, sizeof (line), "f");
					bool fok = true;
					for (uint c = 0; c < 3; c++)
					{
						const u32 *e = vi + 3 * tri[3 * t + c];
						const u32 pv = e[0] < pn ? pm[e[0]] : 0, nv2 = nd && e[1] < nn ? nm[e[1]] : 0, tv = td && e[2] < tn ? tm[e[2]] : 0;
						if (!pv)
							fok = false;
						if (tv && nv2)
							ln += snprintf (line + ln, sizeof (line) - ln, " %u/%u/%u", pv, tv, nv2);
						else if (nv2)
							ln += snprintf (line + ln, sizeof (line) - ln, " %u//%u", pv, nv2);
						else if (tv)
							ln += snprintf (line + ln, sizeof (line) - ln, " %u/%u", pv, tv);
						else
							ln += snprintf (line + ln, sizeof (line) - ln, " %u", pv);
					}
					if (fok)
					{
						njm_put (&o, "%s\n", line);
						ntri++;
					}
				}
				vbase += cnt;
				nbase += ncnt;
				tbase += tcnt;
				ndraw++;
			}
			FREE (pu);
			FREE (nu);
			FREE (tu);
			FREE (pm);
			FREE (nm);
			FREE (tm);
		}
		FREE (vi);
		FREE (tri);
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (o.b && ndraw)
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE COD-GEOM:%s (%u draws, %u triangles) -> %s/model.obj\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ndraw, ntri, dest);
		err = testmode ? ERR_OK : shade_write (arg, dest, basedir, "model.obj", (u8 *)o.b, (uint)o.used);
	}
	FREE (o.b);
	FREE (dc);
	FREE (fx);
	FREE (d);
	return err;
}
