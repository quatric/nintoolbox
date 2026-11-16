// Gamebryo 2.x ".nif" scenes with classic geometry blocks (version 20.3.0.9, big-endian; Monster Mayhem, Dance on Broadway ...; the 20.6.0.0 NiMesh files of Pocoyo
// Racing are handled by extract/nif.inc): header "Gamebryo File Format, Version 20.3.0.9\n", u32 LE version, u8 endian (0 = big), u32 user version, u32 LE block count,
// then big-endian: u16 type count, types {u32 length, name}, u16 block type per block, u32 size per block, u32 string count, u32 max length, strings {u32 length, text}, u32 group
// count + groups, the block data back to back, u32 root count + roots. NiNode / NiTriStrips / NiTriShape blocks are NiObjectNET (name u32, extra data refs, controller)
// + NiAVObject (u16 flags, translation, 3x3 rotation, scale, property refs, collision) + children / {data ref, skin ref, materials, active material, dirty flag}.
// NiTriStripsData / NiTriShapeData hold positions, normals, UVs and the strips / triangles. Geometry is written to "<file>.obj" in world space (node transforms applied, skinning
// ignored, one object per shape named after the node); textures are external files and animation blocks are not decoded.
typedef struct nifc_t
{
	const u8 *d;
	size_t size;
	uint n;
	const char **type;
	size_t *off, *len;
	u32 *parent;
} nifc_t;

typedef struct nifc_av_t
{
	float t[3], r[9], s;
} nifc_av_t;

static ccp nifc_typename (const nifc_t *c, uint b)
{
	return b < c->n ? c->type[b] : "";
}

// reads NiObjectNET + NiAVObject; returns the offset behind the AV object (0 on error)
static size_t nifc_av (const nifc_t *c, uint b, nifc_av_t *av, u32 *name)
{
	const u8 *x = c->d + c->off[b];
	const size_t n = c->len[b];
	size_t p = 0;
	if (n < 12)
		return 0;
	if (name)
		*name = rd_be32 (x);
	const u32 ne = rd_be32 (x + 4);
	if (ne > 1000 || 12 + 4 * (size_t)ne + 2 + 52 + 8 > n)
		return 0;
	p = 8 + 4 * (size_t)ne + 4 + 2;
	for (uint i = 0; i < 3; i++, p += 4)
	{
		const u32 u = rd_be32 (x + p);
		memcpy (&av->t[i], &u, 4);
	}
	for (uint i = 0; i < 9; i++, p += 4)
	{
		const u32 u = rd_be32 (x + p);
		memcpy (&av->r[i], &u, 4);
	}
	{
		const u32 u = rd_be32 (x + p);
		memcpy (&av->s, &u, 4);
		p += 4;
	}
	const u32 np = rd_be32 (x + p);
	if (np > 1000 || p + 4 + 4 * (size_t)np + 4 > n)
		return 0;
	p += 4 + 4 * (size_t)np + 4;
	return p;
}

static void nifc_xf (const nifc_av_t *a, const float *in, float *out)
{
	for (uint i = 0; i < 3; i++)
		out[i] = a->t[i] + a->s * (a->r[3 * i] * in[0] + a->r[3 * i + 1] * in[1] + a->r[3 * i + 2] * in[2]);
}

static enumError extract_nifclassic_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".nif"))
		return ERR_NOTHING_TO_DO;
	static const char magic[] = "Gamebryo File Format, Version 20.3.0.9\n";
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	nifc_t c;
	memset (&c, 0, sizeof (c));
	char **strs = 0;
	char *obj = 0;
	uint ns = 0;
	if (sz < 0x60 || memcmp (raw, magic, sizeof (magic) - 1) || raw[0x2b] != 0)
		goto done;
	{
		size_t p = sizeof (magic) - 1 + 4 + 1 + 4;
		c.d = raw;
		c.size = sz;
		c.n = rd_le32 (raw + p);
		p += 4;
		if (!c.n || c.n > 100000 || p + 2 > sz)
			goto done;
		const uint nt = rd_be16 (raw + p);
		p += 2;
		char **types = CALLOC (nt + 1, sizeof (*types));
		c.type = CALLOC (c.n, sizeof (*c.type));
		c.off = CALLOC (c.n, sizeof (*c.off));
		c.len = CALLOC (c.n, sizeof (*c.len));
		c.parent = CALLOC (c.n, sizeof (*c.parent));
		bool ok = types && c.type && c.off && c.len && c.parent;
		for (uint i = 0; ok && i < nt; i++)
		{
			if (p + 4 > sz)
				ok = false;
			else
			{
				const u32 l = rd_be32 (raw + p);
				if (l > 256 || p + 4 + l > sz)
					ok = false;
				else
				{
					types[i] = MALLOC (l + 1);
					if (!types[i])
						ok = false;
					else
						memcpy (types[i], raw + p + 4, l), types[i][l] = 0;
					p += 4 + l;
				}
			}
		}
		if (ok && p + 2 * (size_t)c.n + 4 * (size_t)c.n + 12 > sz)
			ok = false;
		size_t tp = p;
		if (ok)
		{
			p += 2 * (size_t)c.n;
			for (uint i = 0; ok && i < c.n; i++)
			{
				const uint t = rd_be16 (raw + tp + 2 * (size_t)i);
				if (t >= nt || !types[t])
					ok = false;
				else
					c.type[i] = types[t];
				c.len[i] = rd_be32 (raw + p + 4 * (size_t)i);
			}
			p += 4 * (size_t)c.n;
		}
		if (ok)
		{
			ns = rd_be32 (raw + p);
			p += 8;
			if (ns > 100000)
				ok = false;
		}
		strs = ok ? CALLOC (ns + 1, sizeof (*strs)) : 0;
		for (uint i = 0; ok && i < ns; i++)
		{
			if (!strs || p + 4 > sz)
				ok = false;
			else
			{
				const u32 l = rd_be32 (raw + p);
				if (p + 4 + l > sz)
					ok = false;
				else
				{
					strs[i] = MALLOC (l + 1);
					if (strs[i])
					{
						for (u32 k = 0; k < l; k++)
						{
							const u8 ch = raw[p + 4 + k];
							strs[i][k] = ch > 0x20 && ch < 0x7f && ch != '#' ? ch : '_';
						}
						strs[i][l] = 0;
					}
					p += 4 + l;
				}
			}
		}
		if (ok && p + 4 <= sz)
		{
			const u32 ng = rd_be32 (raw + p);
			p += 4 + 4 * (size_t)ng;
		}
		else
			ok = false;
		for (uint i = 0; ok && i < c.n; i++)
		{
			c.off[i] = p;
			p += c.len[i];
			if (p > sz)
				ok = false;
		}
		if (!ok || p + 4 > sz)
			goto done_c;
		// parent links of the node children
		for (uint i = 0; i < c.n; i++)
			c.parent[i] = (u32)-1;
		for (uint i = 0; i < c.n; i++)
		{
			if (strcmp (c.type[i], "NiNode") && strcmp (c.type[i], "BSFadeNode") && strcmp (c.type[i], "NiBillboardNode"))
				continue;
			nifc_av_t av;
			const size_t q = nifc_av (&c, i, &av, 0);
			if (!q || q + 4 > c.len[i])
				continue;
			const u32 nc = rd_be32 (c.d + c.off[i] + q);
			if (nc > 10000 || q + 4 + 4 * (size_t)nc > c.len[i])
				continue;
			for (u32 k = 0; k < nc; k++)
			{
				const u32 ch = rd_be32 (c.d + c.off[i] + q + 4 + 4 * (size_t)k);
				if (ch < c.n)
					c.parent[ch] = i;
			}
		}
		size_t cap = 1 << 16, used = 0;
		obj = MALLOC (cap);
		if (!obj)
			goto done_c;
		uint nshapes = 0, ntri = 0, vbase = 1;
#define NIFC_EMIT(...) \
	do { \
		for (;;) { \
			const int w_ = snprintf (obj + used, cap - used, __VA_ARGS__); \
			if (w_ >= 0 && (size_t)w_ < cap - used) { used += (size_t)w_; break; } \
			char *nb_ = REALLOC (obj, cap * 2); \
			if (!nb_) { ok = false; break; } \
			obj = nb_; cap *= 2; \
		} \
	} while (0)
		for (uint b = 0; b < c.n && ok; b++)
		{
			const bool strip = !strcmp (c.type[b], "NiTriStrips");
			if (!strip && strcmp (c.type[b], "NiTriShape"))
				continue;
			nifc_av_t av;
			u32 name = 0;
			size_t q = nifc_av (&c, b, &av, &name);
			if (!q || q + 8 > c.len[b])
				continue;
			const u32 dref = rd_be32 (c.d + c.off[b] + q);
			if (dref >= c.n || (strcmp (c.type[dref], "NiTriStripsData") && strcmp (c.type[dref], "NiTriShapeData")))
				continue;
			const u8 *x = c.d + c.off[dref];
			const size_t n = c.len[dref];
			size_t r = 0;
			if (n < 12)
				continue;
			const uint nv = rd_be16 (x + 4);
			r = 9;
			if (!nv || !x[8] || r + 12 * (size_t)nv + 3 > n)
				continue;
			const u8 *V = x + r;
			r += 12 * (size_t)nv;
			const uint flags = rd_be16 (x + r);
			r += 2;
			const uint nuv = flags & 0x3f, nbt = (flags >> 12) & 3;
			const bool hn = x[r++];
			const u8 *N = x + r;
			if (hn)
				r += 12 * (size_t)nv * (nbt ? 3 : 1);
			r += 16;
			if (r + 1 > n)
				continue;
			const bool hc = x[r++];
			if (hc)
				r += 16 * (size_t)nv;
			const u8 *UV = x + r;
			r += 8 * (size_t)nv * nuv + 6;
			if (r + 4 > n)
				continue;
			const uint ntr = rd_be16 (x + r);
			r += 2;
			// faces
			uint nfi = 0;
			u32 *idx = 0;
			if (strip)
			{
				const uint nst = rd_be16 (x + r);
				r += 2;
				if (r + 2 * (size_t)nst + 1 > n)
					continue;
				const u8 *lens = x + r;
				r += 2 * (size_t)nst + 1;
				idx = MALLOC (3 * (size_t)(ntr + nst + 8) * sizeof (u32));
				if (!idx)
					continue;
				bool good = true;
				for (uint s = 0; s < nst && good; s++)
				{
					const uint l = rd_be16 (lens + 2 * (size_t)s);
					if (r + 2 * (size_t)l > n)
					{
						good = false;
						break;
					}
					for (uint k = 2; k < l; k++)
					{
						const u32 a = rd_be16 (x + r + 2 * (size_t)(k - 2)), bb = rd_be16 (x + r + 2 * (size_t)(k - 1)), cc = rd_be16 (x + r + 2 * (size_t)k);
						if (a == bb || bb == cc || a == cc)
							continue;
						if (nfi + 3 > 3 * (ntr + nst + 8))
							break;
						if (k & 1)
							idx[nfi++] = bb, idx[nfi++] = a, idx[nfi++] = cc;
						else
							idx[nfi++] = a, idx[nfi++] = bb, idx[nfi++] = cc;
					}
					r += 2 * (size_t)l;
				}
				if (!good)
				{
					FREE (idx);
					continue;
				}
			}
			else
			{
				if (r + 5 > n)
					continue;
				const u32 npts = rd_be32 (x + r);
				r += 4;
				if (!x[r++] || npts != 3 * ntr || r + 2 * (size_t)npts > n)
					continue;
				idx = MALLOC ((size_t)npts * sizeof (u32) + 4);
				if (!idx)
					continue;
				for (uint k = 0; k < npts; k++)
					idx[nfi++] = rd_be16 (x + r + 2 * (size_t)k);
			}
			bool inrange = nfi > 0;
			for (uint k = 0; k < nfi; k++)
				if (idx[k] >= nv)
					inrange = false;
			if (!inrange)
			{
				FREE (idx);
				continue;
			}
			// world transform chain (shape first, then its ancestors)
			nifc_av_t chain[64];
			uint depthc = 0;
			chain[depthc++] = av;
			for (u32 pa = c.parent[b]; pa != (u32)-1 && depthc < 64; pa = c.parent[pa])
			{
				nifc_av_t pv;
				if (!nifc_av (&c, pa, &pv, 0))
					break;
				chain[depthc++] = pv;
			}
			ccp nm = name < ns && strs[name] ? strs[name] : "shape";
			NIFC_EMIT ("o %s_%u\n", nm, b);
			for (uint i = 0; i < nv && ok; i++)
			{
				float v[3], w[3];
				for (uint a = 0; a < 3; a++)
				{
					const u32 u = rd_be32 (V + 12 * (size_t)i + 4 * a);
					memcpy (&v[a], &u, 4);
					if (!isfinite (v[a]))
						v[a] = 0;
				}
				for (uint k = 0; k < depthc; k++)
				{
					nifc_xf (&chain[k], v, w);
					memcpy (v, w, sizeof (v));
				}
				NIFC_EMIT ("v %.5f %.5f %.5f\n", v[0], v[1], v[2]);
			}
			if (nuv)
				for (uint i = 0; i < nv && ok; i++)
				{
					float t[2];
					for (uint a = 0; a < 2; a++)
					{
						const u32 u = rd_be32 (UV + 8 * (size_t)i + 4 * a);
						memcpy (&t[a], &u, 4);
						if (!isfinite (t[a]))
							t[a] = 0;
					}
					NIFC_EMIT ("vt %.5f %.5f\n", t[0], 1.0 - t[1]);
				}
			for (uint k = 0; k + 2 < nfi + 0 && ok; k += 3)
			{
				if (nuv)
					NIFC_EMIT ("f %u/%u %u/%u %u/%u\n", vbase + idx[k], vbase + idx[k], vbase + idx[k + 1], vbase + idx[k + 1], vbase + idx[k + 2], vbase + idx[k + 2]);
				else
					NIFC_EMIT ("f %u %u %u\n", vbase + idx[k], vbase + idx[k + 1], vbase + idx[k + 2]);
			}
			(void)N;
			vbase += nv;
			nshapes++;
			ntri += nfi / 3;
			FREE (idx);
		}
		if (ok && ntri)
		{
			char dest[PATH_MAX];
			snprintf (dest, sizeof (dest), "%s.obj", arg);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT NIF:%s (%u shapes, %u triangles) -> OBJ:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nshapes, ntri, dest);
			err = ERR_OK;
			if (!testmode)
			{
				File_t F;
				err = CreateFileOpt (&F, true, dest, false, arg);
				if (F.f && fwrite (obj, 1, used, F.f) != used)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)used, dest);
				ResetFile (&F, opt_preserve);
			}
		}
	done_c:
		for (uint i = 0; i < nt; i++)
			FREE (types[i]);
		FREE (types);
	}
done:
	if (strs)
		for (uint i = 0; i < ns; i++)
			FREE (strs[i]);
	FREE (strs);
	FREE (c.type);
	FREE (c.off);
	FREE (c.len);
	FREE (c.parent);
	FREE (obj);
	FREE (raw);
	return err;
}
