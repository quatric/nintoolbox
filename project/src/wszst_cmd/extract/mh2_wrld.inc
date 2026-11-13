// Manhunt 2 (Wii) world geometry (".bsp", the unpacked "WRLD" of an MH2Z file, big endian), vertices only. The world references vertex buffers through 24-byte records {u16 id, u16 users,
// u32 size, u32 data offset, 2 x u32 0}; a buffer is an array of 16-byte entries {f32 x, y, z, 0}. Every buffer found this way is written as one "o" group of "v" lines to
// "<name>.obj". The polygon and index tables of the world are not decoded, so the OBJ holds points and no faces.
// position of vertex i, component k of a stream at q (vertex size vs, data starting al bytes after the 4-byte stream header): f32 (format 0x40) or s16 with the binary point given
// by the low nibble of the format byte
static float mh2w_pos (const u8 *c, size_t q, uint vs, uint al, uint i, int k)
{
	if (c[q + 2] == 0x40)
		return mh2inst_f (c + q + 4 + al + (size_t)vs * i + 4 * k, true);
	return (float)(s16)rd_be16 (c + q + 4 + al + (size_t)vs * i + 2 * k) / (float)(1u << (c[q + 2] & 15));
}

// normal of vertex i, component k of an s16 stream with 6 components (the three s16 after the position)
static float mh2w_nrm (const u8 *c, size_t q, uint vs, uint al, uint i, int k)
{
	return (float)(s16)rd_be16 (c + q + 4 + al + (size_t)vs * i + 6 + 2 * k) / (float)(1u << (c[q + 2] & 15));
}

// one face line: position index, optional UV index and optional normal index (the same as the position index) per vertex
static void mh2w_face (sumo_buf_t *b, bool hasn, uint ntotal_minus_vtotal, uint n, const uint *v, const uint *u)
{
	sumo_printf (b, "f");
	for (uint i = 0; i < n; i++)
	{
		if (u && u[i])
			sumo_printf (b, hasn ? " %u/%u/%u" : " %u/%u", v[i], u[i], v[i] + ntotal_minus_vtotal);
		else if (hasn)
			sumo_printf (b, " %u//%u", v[i], v[i] + ntotal_minus_vtotal);
		else
			sumo_printf (b, " %u", v[i]);
	}
	sumo_printf (b, "\n");
}

// mean UV distance between consecutive display list vertices of one display list, using the u16 UV index at byte offset uvoff of every vertex (-1 if an index is out of range)
static double mh2w_uv_score (const u8 *c, size_t uq, uint ucnt, size_t dl, size_t dsz, uint stride, uint uvoff)
{
	double sum = 0;
	uint npairs = 0;
	for (size_t o = dl; o < dl + dsz && npairs < 20000;)
	{
		const u8 op = c[o];
		if (!op)
		{
			o++;
			continue;
		}
		const uint n = rd_be16 (c + o + 1);
		o += 3;
		uint prev = 0;
		for (uint i = 0; i < n; i++)
		{
			const uint iu = rd_be16 (c + o + (size_t)i * stride + uvoff);
			if (iu >= ucnt)
				return -1;
			if (i)
			{
				double e = 0;
				for (int k = 0; k < 2; k++)
				{
					const double d = (double)(s16)rd_be16 (c + uq + 4 + 4 * (size_t)iu + 2 * k) - (s16)rd_be16 (c + uq + 4 + 4 * (size_t)prev + 2 * k);
					e += d * d;
				}
				sum += sqrt (e) / (double)(1u << (c[uq + 2] & 15));
				npairs++;
			}
			prev = iu;
		}
		o += (size_t)n * stride;
	}
	return npairs ? sum / npairs : -1;
}

// checks a display list (GX draws 0x80 quads, 0x90 triangles, 0x98 strips, 0xa0 fans; zero bytes are padding) whose vertices have a constant size; the first u16 of each vertex is a
// position index below cnt. Returns the vertex size, or 0.
static uint mh2w_dl_stride (const u8 *c, size_t dl, size_t dsz, uint cnt)
{
	for (uint stride = 2; stride <= 26; stride++)
	{
		size_t o = dl;
		bool ok = true;
		uint draws = 0;
		while (o < dl + dsz && ok)
		{
			const u8 op = c[o];
			if (!op)
			{
				o++;
				continue;
			}
			if (op != 0x80 && op != 0x90 && op != 0x98 && op != 0xa0)
				break;
			const uint n = rd_be16 (c + o + 1);
			o += 3;
			if (!n || o + (size_t)n * stride > dl + dsz)
			{
				ok = false;
				break;
			}
			for (uint i = 0; i < n; i++)
				if (rd_be16 (c + o + (size_t)i * stride) >= cnt)
					ok = false;
			o += (size_t)n * stride;
			draws++;
		}
		if (ok && draws && o >= dl + dsz)
			return stride;
	}
	return 0;
}

static uint mh2w_asz (uint f)
{
	return f == 3 ? 2 : f ? 1 : 0;
}

static float mh2w_uvv (const u8 *c, size_t uq, uint type, uint shift, bool sgn, uint i, uint k)
{
	const float sc = (float)(1u << shift);
	if (type == 2)
		return c[uq + 2 * (size_t)i + k] / sc;
	if (type == 4)
		return mh2inst_f (c + uq + 8 * (size_t)i + 4 * k, true) / sc;
	const uint v = rd_be16 (c + uq + 4 * (size_t)i + 2 * k);
	return (sgn ? (float)(s16)v : (float)v) / sc;
}

typedef struct
{
	uint n;
	uint bone[8];
	float w[8];
} mh2w_sv_t;

static void mh2w_sv_add (mh2w_sv_t *sv, uint cnt, const u8 *c, size_t data, const u8 *raw, uint bone, float w, bool replace)
{
	for (uint i = 0; i < cnt; i++)
		if (!memcmp (c + data + 12 * (size_t)i, raw, 12))
		{
			if (replace)
				sv[i].n = 0;
			if (sv[i].n < 8)
			{
				sv[i].bone[sv[i].n] = bone;
				sv[i].w[sv[i].n++] = w;
			}
			return;
		}
}

static enumError extract_mh2_wrld_file (ccp arg, ccp basedir, uint depth)
{
	const bool isdff = is_ext (arg, ".dff");
	if (!isdff && !is_ext (arg, ".bsp"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x100 || memcmp (c, isdff ? "CLMP" : "WRLD", 4))
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_buf_t mt = { 0, 0, 0, true };
	sumo_buf_t sk = { 0, 0, 0, true };
	sumo_buf_t bn = { 0, 0, 0, true };
	char stem[PATH_MAX];
	{
		const char *sl = strrchr (arg, '/');
		snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (stem, '.');
		if (dot)
			*dot = 0;
	}
	char mnames[128][64];
	uint nmnames = 0;
	sumo_printf (&b, "mtllib %s.mtl\n", stem);
	uint groups = 0;
	size_t lim = rd_be32 (c + 8);
	if (lim > cs || lim < 0x100)
		lim = cs;
	float bmin[3], bmax[3];
	for (int k = 0; k < 3; k++)
	{
		bmin[k] = isdff ? -1e4f : mh2inst_f (c + 0x30 + 4 * k, true) - 5.0f;
		bmax[k] = isdff ? 1e4f : mh2inst_f (c + 0x40 + 4 * k, true) + 5.0f;
	}
	// Sector position streams: {u16 count, format (0x40 f32 or 0x3n s16), 3 or 6 components} followed by the vertices. The sector's display lists are listed by 16-byte table
	// entries {u32 offset from the sector start (stream header - 0x38), u32 size, u32 0x01000000, u32 0x150100nn}; a display list starts 0x1c bytes after its offset. The
	// vertices of s16 streams start at an unknown 2-byte aligned offset: the one with the shortest distance between consecutive display list indices is used.
	size_t sq[512];
	uint scnt[512], svs[512], ns = 0;
	for (size_t q = 0x40; q + 4 <= lim && ns < 512; q += 4)
	{
		const uint cnt = rd_be16 (c + q);
		const uint fmt = c[q + 2];
		const uint comps = c[q + 3];
		const uint vs = fmt == 0x40 ? 12 : 2 * comps;
		if (cnt < 3 || (fmt != 0x40 && (fmt >> 4) != 3) || (comps != 3 && comps != 6) || (fmt == 0x40 && comps != 3) || q < 0x38 + 0x40
			|| rd_be32 (c + q - 0x38) != 0x00b749e0 || q + 4 + (size_t)cnt * vs > cs)
			continue;
		bool good = true;
		uint nonzero = 0;
		for (uint i = 0; i < cnt && good; i++)
			for (int k = 0; k < 3 && good; k++)
			{
				const float f = mh2w_pos (c, q, vs, 0, i, k);
				if (fmt == 0x40 && !(f >= bmin[k] && f <= bmax[k]))
					good = false;
				if (f != 0.0f)
					nonzero++;
			}
		if (!good || nonzero < cnt)
			continue;
		sq[ns] = q;
		scnt[ns] = cnt;
		svs[ns++] = vs;
		q += (size_t)cnt * vs;
	}
	uint vtotal = 0, ntotal = 0, utotal = 0;
	for (uint si = 0; si < ns; si++)
	{
		const size_t q = sq[si];
		const uint cnt = scnt[si], vs = svs[si];
		const size_t base = q - 0x38;
		// The mesh record points at the sector start (+108); its first words are the texture table (u32 entries pointer, u32 count; 12-byte entries starting with a name pointer).
		// A display list's material index (low half of its table entry) selects the texture.
		ccp texn[64];
		uint ntex = 0;
		size_t rec = 0;
		for (size_t p = 108; p + 4 <= cs && !ntex; p += 4)
			if (rd_be32 (c + p) == base)
			{
				const size_t r = p - 108, tp = rd_be32 (c + r), tc = rd_be32 (c + r + 4);
				if (!tc || tc > 64 || tp < 0x20 || tp + tc * 12 > cs)
					continue;
				uint good = 0;
				for (uint i = 0; i < tc; i++)
				{
					const size_t np = rd_be32 (c + tp + 12 * i);
					size_t l = 0;
					while (np && np < cs && l < 60 && c[np + l] >= 32 && c[np + l] < 127)
						l++;
					if (l && np + l < cs && !c[np + l])
					{
						texn[good++] = (ccp)c + np;
						continue;
					}
					good = 0;
					break;
				}
				ntex = good;
				if (good)
					rec = r;
			}
		const size_t tend = si + 1 < ns ? sq[si + 1] : lim;
		size_t dls[256], dss[256], tfirst = 0;
		uint mask = 0;
		uint dst[256], dmat[256], dbone[256], nd = 0;
		for (size_t t = q + 4 + (size_t)cnt * vs; t + 16 <= tend && nd < 256; t += 4)
		{
			const bool marked = rd_be32 (c + t + 8) == 0x01000000 && (rd_be32 (c + t + 12) >> 8) == 0x150100;
			if (!marked && (rd_be32 (c + t + 8) || rd_be32 (c + t + 12)))
				continue;
			const size_t dl = base + rd_be32 (c + t) + 0x1c, dsz = rd_be32 (c + t + 4);
			if (!dsz || dsz % 32 || dl + dsz > cs)
				continue;
			const bool firste = !tfirst;
			if (firste)
			{
				tfirst = t;
				mask = rd_be16 (c + t - 2);
			}
			bool dup = false;
			for (uint e = 0; e < nd; e++)
				if (dls[e] == dl)
					dup = true;
			if (dup)
				continue;
			const uint stride = mh2w_dl_stride (c, dl, dsz, cnt);
			if (!stride)
				continue;
			dls[nd] = dl;
			dss[nd] = dsz;
			dmat[nd] = firste ? 0 : rd_be16 (c + t - 2);
			dbone[nd] = c[t - 3];
			dst[nd++] = stride;
		}
		uint al = 0;
		if (c[q + 2] != 0x40 && nd)
		{
			double best = 1e30;
			for (uint a2 = 0; a2 <= 10; a2 += 2)
			{
				if (q + 4 + a2 + (size_t)cnt * vs > cs)
					break;
				double sum = 0;
				uint npairs = 0;
				for (uint d = 0; d < nd && npairs < 20000; d++)
				{
					size_t o = dls[d];
					while (o < dls[d] + dss[d] && npairs < 20000)
					{
						const u8 op = c[o];
						if (!op)
						{
							o++;
							continue;
						}
						const uint n = rd_be16 (c + o + 1);
						o += 3;
						for (uint i = 0; i + 1 < n; i++)
						{
							const uint i0 = rd_be16 (c + o + (size_t)i * dst[d]), i1 = rd_be16 (c + o + (size_t)(i + 1) * dst[d]);
							double e = 0;
							for (int k = 0; k < 3; k++)
							{
								const double dd = (double)mh2w_pos (c, q, vs, a2, i0, k) - mh2w_pos (c, q, vs, a2, i1, k);
								e += dd * dd;
							}
							sum += sqrt (e);
							npairs++;
						}
						o += (size_t)n * dst[d];
					}
				}
				if (npairs && sum / npairs < best)
				{
					best = sum / npairs;
					al = a2;
				}
			}
		}
		// The first display list entry's u16 (the 2 bytes before the entry) is the vertex descriptor: 2 bits per attribute (matrix index, position, normal, colour 0, colour 1, UV 0, UV 1, UV 2),
		// 1 and 2 being a byte index and 3 a u16 index. The six u32 at sector + 0x1c are the offsets (from sector + 0x1c) of the stream headers {u32 data offset, u16 count, u16 type << 12 | shift << 8}.
		const size_t p3 = base + 0x1c;
		uint mf[8], moff[9], mstride = 0;
		for (uint k = 0; k < 8; k++)
		{
			mf[k] = (mask >> (2 * k)) & 3;
			moff[k] = mstride;
			mstride += mh2w_asz (mf[k]);
		}
		moff[8] = mstride;
		bool mok = nd && mstride && !mf[0] && mf[1] == 3;
		for (uint d = 0; d < nd; d++)
			if (dst[d] != mstride)
				mok = false;
		u8 *vcol = 0;
		if (mok && mf[3] && p3 + 24 <= cs)
		{
			const size_t ch = p3 + rd_be32 (c + p3 + 4);
			if (ch + 8 <= cs)
			{
				const size_t cd = p3 + rd_be32 (c + ch);
				const uint cc = rd_be16 (c + ch + 4), ct = rd_be16 (c + ch + 6) >> 12;
				if (cc && (ct == 2 || ct == 5) && cd + (size_t)cc * 4 <= cs && (vcol = CALLOC (cnt, 4)))
				{
					memset (vcol, 255, (size_t)cnt * 4);
					for (uint d = 0; d < nd; d++)
					{
						size_t o = dls[d];
						while (o < dls[d] + dss[d])
						{
							if (!c[o])
							{
								o++;
								continue;
							}
							const uint n = rd_be16 (c + o + 1);
							o += 3;
							for (uint i = 0; i < n && o + (size_t)(i + 1) * mstride <= cs; i++)
							{
								const u8 *v = c + o + (size_t)i * mstride;
								const uint pi = rd_be16 (v + moff[1]), ci = mf[3] == 3 ? rd_be16 (v + moff[3]) : v[moff[3]];
								if (pi < cnt && ci < cc)
								{
									memcpy (vcol + 4 * (size_t)pi, c + cd + 4 * (size_t)ci, 3);
									vcol[4 * (size_t)pi + 3] = ct == 5 ? c[cd + 4 * (size_t)ci + 3] : 255;
								}
							}
							o += (size_t)n * mstride;
						}
					}
				}
			}
		}
		sumo_printf (&b, "o sector%04u_%08x\n", groups++, (uint)q);
		for (uint i = 0; i < cnt; i++)
			if (vcol)
				sumo_printf (&b, "v %g %g %g %g %g %g\n", mh2w_pos (c, q, vs, al, i, 0), mh2w_pos (c, q, vs, al, i, 1), mh2w_pos (c, q, vs, al, i, 2), vcol[4 * (size_t)i] / 255.0f,
					vcol[4 * (size_t)i + 1] / 255.0f, vcol[4 * (size_t)i + 2] / 255.0f);
			else
				sumo_printf (&b, "v %g %g %g\n", mh2w_pos (c, q, vs, al, i, 0), mh2w_pos (c, q, vs, al, i, 1), mh2w_pos (c, q, vs, al, i, 2));
		const bool hasn = c[q + 2] != 0x40 && vs == 12;
		if (hasn)
			for (uint i = 0; i < cnt; i++)
				sumo_printf (&b, "vn %g %g %g\n", mh2w_nrm (c, q, vs, al, i, 0), mh2w_nrm (c, q, vs, al, i, 1), mh2w_nrm (c, q, vs, al, i, 2));
		// UV stream: {u16 count, 0x3n, 2} s16 pairs; chosen together with the byte offset of the UV index inside the vertices of the first display list
		size_t uvq = 0;
		uint uvcnt = 0, uvoff = 0, uvstride = 0, uvtype = 3, uvshift = 11;
		bool uvsgn = true, uvbyte = false;
		if (mok && mf[5] && p3 + 24 <= cs)
		{
			const size_t uh = p3 + rd_be32 (c + p3 + 8);
			if (uh + 8 <= cs)
			{
				const size_t ud = p3 + rd_be32 (c + uh);
				const uint uc = rd_be16 (c + uh + 4), uf = rd_be16 (c + uh + 6), ut = uf >> 12, usz = ut == 2 ? 2 : ut == 3 ? 4 : ut == 4 ? 8 : 0;
				if (uc > 1 && usz && ud + (size_t)uc * usz <= cs)
				{
					uvq = ud;
					uvcnt = uc;
					uvoff = moff[5];
					uvstride = mstride;
					uvtype = ut;
					uvshift = (uf >> 8) & 15;
					uvsgn = false;
					uvbyte = mf[5] != 3;
				}
			}
		}
		if (nd && !uvq)
		{
			double best = 0.3;
			for (size_t uo = q + 4 + (size_t)cnt * vs; uo + 4 <= tend && uo + 4 <= cs; uo += 2)
			{
				const uint ucnt = rd_be16 (c + uo);
				if (ucnt < 3 || (c[uo + 2] >> 4) != 3 || c[uo + 3] != 2 || uo + 4 + (size_t)ucnt * 4 > cs)
					continue;
				for (uint off = 2; off + 2 <= dst[0]; off++)
				{
					const double sc = mh2w_uv_score (c, uo, ucnt, dls[0], dss[0], dst[0], off);
					if (sc >= 0 && sc < best)
					{
						best = sc;
						uvq = uo;
						uvcnt = ucnt;
						uvoff = off;
						uvstride = dst[0];
						uvtype = 3;
						uvshift = c[uo + 2] & 15;
					}
				}
			}
		}
		if (uvq)
			for (uint i = 0; i < uvcnt; i++)
				sumo_printf (&b, "vt %g %g\n", mh2w_uvv (c, uvq + (uvsgn ? 4 : 0), uvtype, uvshift, uvsgn, i, 0), 1.0f - mh2w_uvv (c, uvq + (uvsgn ? 4 : 0), uvtype, uvshift, uvsgn, i, 1));
		for (uint d = 0; d < nd; d++)
		{
			size_t o = dls[d];
			const uint stride = dst[d];
			{
				char mn[64];
				if (dmat[d] < ntex)
					snprintf (mn, sizeof (mn), "%s", texn[dmat[d]]);
				else
					snprintf (mn, sizeof (mn), "material%u", dmat[d]);
				for (char *ch = mn; *ch; ch++)
					if (*ch == ' ')
						*ch = '_';
				uint mi;
				for (mi = 0; mi < nmnames && strcmp (mnames[mi], mn); mi++)
					;
				if (mi == nmnames && nmnames < 128)
				{
					snprintf (mnames[nmnames++], sizeof (mnames[0]), "%s", mn);
					sumo_printf (&mt, "newmtl %s\nKd 1 1 1\n", mn);
					if (dmat[d] < ntex)
						sumo_printf (&mt, "map_Kd %s.png\n", mn);
				}
				sumo_printf (&b, "usemtl %s\n", mn);
			}
			while (o < dls[d] + dss[d])
			{
				const u8 op = c[o];
				if (!op)
				{
					o++;
					continue;
				}
				const uint n = rd_be16 (c + o + 1);
				o += 3;
				uint ix[4096], iu[4096];
				const uint m = n < 4096 ? n : 4096;
				for (uint i = 0; i < m; i++)
				{
					ix[i] = vtotal + 1 + rd_be16 (c + o + (size_t)i * stride);
					iu[i] = uvq && stride == uvstride ? utotal + 1 + (uvbyte ? c[o + (size_t)i * stride + uvoff] : rd_be16 (c + o + (size_t)i * stride + uvoff)) : 0;
				}
				o += (size_t)n * stride;
				if (op == 0x90)
					for (uint i = 0; i + 2 < m; i += 3)
						mh2w_face (&b, hasn, ntotal - vtotal, 3, ix + i, iu + i);
				else if (op == 0x98)
					for (uint i = 0; i + 2 < m; i++)
					{
						const uint t3[3] = { ix[i], i & 1 ? ix[i + 2] : ix[i + 1], i & 1 ? ix[i + 1] : ix[i + 2] };
						const uint u3[3] = { iu[i], i & 1 ? iu[i + 2] : iu[i + 1], i & 1 ? iu[i + 1] : iu[i + 2] };
						mh2w_face (&b, hasn, ntotal - vtotal, 3, t3, u3);
					}
				else if (op == 0xa0)
					for (uint i = 1; i + 1 < m; i++)
					{
						const uint t3[3] = { ix[0], ix[i], ix[i + 1] };
						const uint u3[3] = { iu[0], iu[i], iu[i + 1] };
						mh2w_face (&b, hasn, ntotal - vtotal, 3, t3, u3);
					}
				else
					for (uint i = 0; i + 3 < m; i += 4)
						mh2w_face (&b, hasn, ntotal - vtotal, 4, ix + i, iu + i);
			}
		}
		FREE (vcol);
		sumo_printf (&sk, "o sector%04u_%08x\n", groups - 1, (uint)q);
		for (uint d = 0; d < nd; d++)
			sumo_printf (&sk, "d %u bone %u material %u\n", d, dbone[d], dmat[d]);
		if (rec && hasn && c[q + 2] != 0x40)
		{
			const size_t sp = rd_be32 (c + rec + 116), data = q + 8;
			mh2w_sv_t *sv = sp >= 0x20 && sp + 32 <= cs ? CALLOC (cnt, sizeof (*sv)) : 0;
			if (sv)
			{
				const uint n1 = rd_be16 (c + sp), n2 = rd_be16 (c + sp + 2), n3 = rd_be16 (c + sp + 4);
				size_t p = sp + rd_be32 (c + sp + 8);
				for (uint g = 0; g < n1 && p + 64 <= cs; g++, p += 64)
				{
					const size_t vd = sp + rd_be32 (c + p + 48) + c[p + 60];
					const uint bone = rd_be16 (c + p + 56), n = rd_be16 (c + p + 58);
					for (uint j = 0; j < n && vd + (size_t)(j + 1) * 12 <= cs; j++)
						mh2w_sv_add (sv, cnt, c, data, c + vd + 12 * (size_t)j, bone, 1.0f, true);
				}
				p = sp + rd_be32 (c + sp + 12);
				for (uint g = 0; g < n2 && p + 116 <= cs; g++, p += 116)
				{
					const size_t vd = sp + rd_be32 (c + p + 96) + c[p + 114], wd = sp + rd_be32 (c + p + 100);
					const uint ba = rd_be16 (c + p + 108), bb = rd_be16 (c + p + 110), n = rd_be16 (c + p + 112);
					for (uint j = 0; j < n && vd + (size_t)(j + 1) * 12 <= cs && wd + 2 * (size_t)(j + 1) <= cs; j++)
					{
						mh2w_sv_add (sv, cnt, c, data, c + vd + 12 * (size_t)j, ba, c[wd + 2 * (size_t)j] / 256.0f, false);
						mh2w_sv_add (sv, cnt, c, data, c + vd + 12 * (size_t)j, bb, c[wd + 2 * (size_t)j + 1] / 256.0f, false);
					}
				}
				p = sp + rd_be32 (c + sp + 16);
				for (uint g = 0; g < n3 && p + 68 <= cs; g++, p += 68)
				{
					const size_t io = sp + rd_be32 (c + p + 52), wo = sp + rd_be32 (c + p + 60);
					const uint bone = rd_be16 (c + p + 64), n = rd_be16 (c + p + 66);
					for (uint j = 0; j < n && io + 2 * (size_t)(j + 1) <= cs && wo + (size_t)j < cs; j++)
					{
						const uint vi = rd_be16 (c + io + 2 * (size_t)j);
						if (vi < cnt && sv[vi].n < 8)
						{
							sv[vi].bone[sv[vi].n] = bone;
							sv[vi].w[sv[vi].n++] = c[wo + j] / 256.0f;
						}
					}
				}
				for (uint i = 0; i < cnt; i++)
					if (sv[i].n)
					{
						sumo_printf (&sk, "w %u", i);
						for (uint k = 0; k < sv[i].n; k++)
							sumo_printf (&sk, " %u:%g", sv[i].bone[k], sv[i].w[k]);
						sumo_printf (&sk, "\n");
					}
				FREE (sv);
			}
		}
		vtotal += cnt;
		if (hasn)
			ntotal += cnt;
		utotal += uvcnt;
	}
	// Character models: u32 at 0x20 is the first model header {u32 next (0x20 ends), ?, u32 bone block}; the bone block starts with the root frame pointer. A frame is
	// {u32 id, u32 next sibling, u32 parent, u32 root, u32 first child, u32 ?, char name[40], two 4x4 float matrices (local, then identity)}; the walk takes the child before the sibling.
	if (isdff)
	{
		size_t h = rd_be32 (c + 0x20);
		for (uint mi = 0; h >= 0x24 && h + 12 <= cs && mi < 64; mi++)
		{
			const size_t nx = rd_be32 (c + h), bb = rd_be32 (c + h + 8);
			sumo_printf (&bn, "model %u\n", mi);
			if (bb >= 0x20 && bb + 20 <= cs)
			{
				size_t stack[512], seenb[512];
				uint sp = 0, nb = 0;
				stack[sp++] = rd_be32 (c + bb);
				while (sp && nb < 512)
				{
					const size_t n = stack[--sp];
					bool dupb = false;
					for (uint k = 0; k < nb; k++)
						if (seenb[k] == n)
							dupb = true;
					if (dupb || n < 0x20 || n + 192 > cs)
						continue;
					seenb[nb] = n;
					sumo_printf (&bn, "bone %u offset %x id %u name %.40s sibling %x parent %x root %x child %x extra %x\n", nb, (uint)n, rd_be32 (c + n), (ccp)c + n + 24, rd_be32 (c + n + 4), rd_be32 (c + n + 8),
						rd_be32 (c + n + 12), rd_be32 (c + n + 16), rd_be32 (c + n + 20));
					for (uint m2 = 0; m2 < 2; m2++)
					{
						sumo_printf (&bn, "matrix");
						for (uint k = 0; k < 16; k++)
							sumo_printf (&bn, " %g", mh2inst_f (c + n + 64 + 64 * m2 + 4 * k, true));
						sumo_printf (&bn, "\n");
					}
					nb++;
					if (sp < 510)
					{
						stack[sp++] = rd_be32 (c + n + 4);
						stack[sp++] = rd_be32 (c + n + 16);
					}
				}
			}
			if (nx == 0x20)
				break;
			h = nx;
		}
	}
	u32 seen[512];
	uint nseen = 0;
	for (size_t o = 0x40; o + 16 <= lim; o += 4)
		for (int variant = 0; variant < 2; variant++)
		{
			const u32 w1 = rd_be32 (c + o + 4), w2 = rd_be32 (c + o + 8);
			const u32 size = variant ? w2 : w1, off = variant ? w1 : w2;
			if (!size || size % 16 || size > 0x400000 || off < 0x40 || (size_t)off + size > cs)
				continue;
			uint k2;
			for (k2 = 0; k2 < nseen && seen[k2] != off; k2++)
				;
			if (k2 < nseen || nseen == 512)
				continue;
			bool good = true;
			uint nonzero = 0;
			for (u32 i = 0; i < size && good; i += 16)
			{
				const u8 *v = c + off + i;
				if (rd_be32 (v + 12))
					good = false;
				for (int k = 0; k < 3 && good; k++)
				{
					const float f = mh2inst_f (v + 4 * k, true);
					if (!(f >= bmin[k] && f <= bmax[k]))
						good = false;
					if (f != 0.0f)
						nonzero++;
				}
			}
			if (!good || !nonzero)
				continue;
			seen[nseen++] = off;
			sumo_printf (&b, "o buffer%04u_%08x\n", groups++, off);
			for (u32 i = 0; i < size; i += 16)
				sumo_printf (&b, "v %g %g %g\n", mh2inst_f (c + off + i, true), mh2inst_f (c + off + i + 4, true), mh2inst_f (c + off + i + 8, true));
		}
	FREE (c);
	enumError err = ERR_NOTHING_TO_DO;
	if (b.ok && groups && b.n < 0xffffffffu)
	{
		char name[PATH_MAX], mname[PATH_MAX];
		snprintf (name, sizeof (name), "%s.obj", stem);
		snprintf (mname, sizeof (mname), "%s.mtl", stem);
		nintendo_sarc_entry_t *entries = CALLOC (4, sizeof (*entries));
		char bname[PATH_MAX];
		snprintf (bname, sizeof (bname), "%s.bones.txt", stem);
		char sname[PATH_MAX];
		snprintf (sname, sizeof (sname), "%s.skin.txt", stem);
		if (entries && OwnedEntryAdd (entries, 0, name, (u8 *)b.p, (uint)b.n) && OwnedEntryAdd (entries, 1, mname, (u8 *)mt.p, (uint)mt.n))
		{
			uint ne = 2;
			if (sk.ok && sk.n && OwnedEntryAdd (entries, ne, sname, (u8 *)sk.p, (uint)sk.n))
				ne++;
			if (isdff && bn.ok && bn.n && OwnedEntryAdd (entries, ne, bname, (u8 *)bn.p, (uint)bn.n))
				ne++;
			err = write_owned_entries (arg, basedir, depth, isdff ? "MH2-DFF" : "MH2-WRLD", entries, ne);
		}
		else
			FREE (entries);
	}
	else
		FREE (b.p);
	return err;
}
