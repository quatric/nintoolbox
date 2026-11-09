// Ubisoft Imagine "wiz" static model (Beastly, Babyz, ...; the 3D objects, usually LZSS wrapped, see imaginelz.inc), big-endian:
//   +0 u32 mesh blob size (the texture table follows it), +4 u32 offset of the mesh table, +0x34.. bounding data,
//   mesh table: u32 count, count x u32 record offset (absolute).
// Mesh record (R): +0 / +4 / +8 / +0xc = offsets (relative to R) of the position (float3), normal, colour (u16) and
// texcoord (float2) arrays, each preceded by its u32 byte size; +0x2c / +0x2e / +0x30 byte vertex attribute modes (every other byte) for the
// present arrays in that order (2 = u8 index, 3 = u16 index); +0x3a u16 position count; the GX display list (commands
// 0x80 quads, 0x90 triangles, 0x98 strip, 0xa0 fan, u16 vertex count, one index per attribute) starts at R + 0x80 and
// ends at the first 0 command byte. Texture table at the mesh blob end: u32 count, u32, count x u32 entry offset; entry:
// u32 data offset, u16 width, u16 height, u32 GX format (14 = CMPR, ...), then the GX data (mip chain ignored).
// Written as "model.obj" (positions, texcoords, faces per mesh), "tex_NNNN.png" and, when the file holds exactly one
// texture, "model.mtl" binding it to every mesh.
static bool wiz_ok (const u8 *raw, size_t sz, size_t o, size_t n)
{
	(void)raw;
	return o <= sz && n <= sz - o;
}

// Display lists of mesh record R: one or more segments (commands up to a 0 byte) from R + 0x80, separated by zero padding, up to the
// next record (REND). Returns true when at least one whole segment is valid for this vertex layout (present attributes, 1 / 2 byte
// indices, index counts); *ENDP is the end of the last valid segment.
static bool wiz_dl_scan (const u8 *raw, size_t sz, size_t R, size_t rend, const bool *pres, const uint *md, const uint *cntv, size_t *endp, size_t (*segs)[2], uint *nseg)
{
	uint stride = 0;
	for (uint k = 0; k < 4; k++)
		if (pres[k])
			stride += md[k] == 3 ? 2 : 1;
	size_t p = R + 0x80, good = 0;
	*nseg = 0;
	if (rend > sz)
		rend = sz;
	while (p + 3 <= rend)
	{
		if (raw[p] != 0x80 && raw[p] != 0x90 && raw[p] != 0x98 && raw[p] != 0xa0)
		{
			if (!good)
				return false; // the first segment must start at R + 0x80
			p = (p + 0x20) & ~(size_t)0x1f; // later segments start on 0x20 boundaries after other data
			continue;
		}
		uint nv = 0;
		bool seg = false;
		size_t q = p;
		while (q + 3 <= rend)
		{
			const uint cmd = raw[q];
			if (!cmd)
			{
				seg = nv > 0;
				break;
			}
			const uint cnt = rd_be16 (raw + q + 1);
			q += 3;
			if ((cmd != 0x80 && cmd != 0x90 && cmd != 0x98 && cmd != 0xa0) || q + (size_t)cnt * stride > rend)
				break;
			bool fit = true;
			for (uint i = 0; i < cnt && fit; i++)
			{
				const u8 *v = raw + q + (size_t)i * stride;
				uint o = 0;
				for (uint k = 0; k < 4; k++)
					if (pres[k])
					{
						const uint x = md[k] == 3 ? rd_be16 (v + o) : v[o];
						o += md[k] == 3 ? 2 : 1;
						fit = fit && x < cntv[k];
					}
			}
			if (!fit)
				break;
			nv += cnt;
			q += (size_t)cnt * stride;
		}
		if (seg)
		{
			if (*nseg < 512)
			{
				segs[*nseg][0] = p;
				segs[*nseg][1] = q;
				(*nseg)++;
			}
			good = q;
			p = (q + 0x1f) & ~(size_t)0x1f;
		}
		else
		{
			if (!good)
				return false;
			p = (p + 0x20) & ~(size_t)0x1f;
		}
	}
	*endp = good;
	return good != 0;
}

static float wiz_f (const u8 *p)
{
	const u32 v = rd_be32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static enumError extract_imaginewiz_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".wiz"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x60 || sz > 0x40000000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 blob = rd_be32 (raw), mt = rd_be32 (raw + 4);
	if (blob < 0x60 || blob > sz || mt < 0x34 || (mt & 3) || !wiz_ok (raw, sz, mt, 8) || blob == 0x21436587)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 nm = rd_be32 (raw + mt);
	if (!nm || nm > 4096 || !wiz_ok (raw, sz, mt + 4, 4 * (size_t)nm))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t cap = 1 << 16, used = 0;
	char *obj = MALLOC (cap);
	if (!obj)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint vbase = 1, tbase = 1, ntri = 0, nmesh = 0, nskip = 0;
	bool ok = true;
#define WIZ_EMIT(...) \
	do { \
		for (;;) { \
			const int w_ = snprintf (obj + used, cap - used, __VA_ARGS__); \
			if (w_ >= 0 && (size_t)w_ < cap - used) { used += (size_t)w_; break; } \
			char *nb_ = REALLOC (obj, cap * 2); \
			if (!nb_) { ok = false; break; } \
			obj = nb_; cap *= 2; \
		} \
	} while (0)
	for (uint m = 0; m < nm && ok; m++)
	{
		const size_t R = rd_be32 (raw + mt + 4 + 4 * m);
		size_t rend = blob;
		for (uint k = 0; k < nm; k++)
		{
			const size_t o = rd_be32 (raw + mt + 4 + 4 * k);
			if (o > R && o < rend)
				rend = o;
		}
		if (!wiz_ok (raw, sz, R, 0x90))
		{
			nskip++;
			continue; // not a mesh record
		}
		const u32 offp = rd_be32 (raw + R), offn = rd_be32 (raw + R + 4), offc = rd_be32 (raw + R + 8), offt = rd_be32 (raw + R + 12);
		const uint npos = rd_be16 (raw + R + 0x3a);
		if (!offp || !npos || npos > 0x20000 || !wiz_ok (raw, sz, R + offp, 12 * (size_t)npos) || raw[R + 0x80] < 0x80)
		{
			nskip++;
			continue; // empty or non-geometry record (no display list)
		}
		uint ntex = 0;
		if (offt)
		{
			const u32 tsz = rd_be32 (raw + R + offt - 4);
			ntex = tsz / 8;
			if (!tsz || !wiz_ok (raw, sz, R + offt, tsz))
			{
				ok = false;
				break;
			}
		}
		// vertex layout: position, [normal], [colour], [texcoord] indices. Without normals the 1 / 2 byte modes are stored at +0x2c (every
		// other byte); with normals (skinned and some level meshes) they are found by testing the combinations against the display list
		uint nnrm = 0, ncol = 0;
		if (offn)
		{
			const u32 nsz = rd_be32 (raw + R + offn - 4);
			nnrm = nsz / 12;
			if (!nsz || !wiz_ok (raw, sz, R + offn, nsz))
			{
				nskip++;
				continue;
			}
		}
		if (offc)
		{
			const u32 csz = rd_be32 (raw + R + offc - 4);
			ncol = csz / 2;
			if (!csz || !wiz_ok (raw, sz, R + offc, csz))
			{
				nskip++;
				continue;
			}
		}
		const bool pres[4] = { true, offn != 0, offc != 0, offt != 0 };
		const uint cntv[4] = { npos, nnrm, ncol, ntex };
		uint md[4] = { 2, 2, 2, 2 };
		bool layout = false;
		size_t dlend = 0, segs[512][2];
		uint nseg = 0;
		{
			// GX attribute modes at +0x2c in descriptor order: position, normal, colour 0, colour 1 (unused), texcoord 0 (0 = absent,
			// 2 = 8 bit index, 3 = 16 bit index)
			static const uint at[4] = { 0x2c, 0x2d, 0x2e, 0x30 };
			bool direct = true;
			for (uint k = 0; k < 4; k++)
			{
				md[k] = raw[R + at[k]];
				if (pres[k] ? md[k] != 2 && md[k] != 3 : md[k] != 0)
					direct = false;
			}
			if (direct)
				layout = wiz_dl_scan (raw, sz, R, rend, pres, md, cntv, &dlend, segs, &nseg);
		}
		if (!layout && !offn)
		{
			// older rule (checked on the Beastly static models): the modes of the present attributes at +0x2c, every other byte
			for (uint k = 0, slot = 0; k < 4; k++)
				if (pres[k])
				{
					md[k] = raw[R + 0x2c + 2 * slot];
					slot++;
				}
			if ((md[0] == 2 || md[0] == 3) && (!pres[2] || md[2] == 2 || md[2] == 3) && (!pres[3] || md[3] == 2 || md[3] == 3))
				layout = wiz_dl_scan (raw, sz, R, rend, pres, md, cntv, &dlend, segs, &nseg);
		}
		for (uint combo = 0; !layout && combo < 16; combo++)
		{
			for (uint k = 0; k < 4; k++)
				md[k] = combo >> k & 1 ? 3 : 2;
			layout = wiz_dl_scan (raw, sz, R, rend, pres, md, cntv, &dlend, segs, &nseg);
		}
		if (!layout)
		{
			nskip++;
			continue; // display list does not fit any vertex layout
		}
		uint stride = 0;
		for (uint k = 0; k < 4; k++)
			if (pres[k])
				stride += md[k] == 3 ? 2 : 1;
		WIZ_EMIT ("o mesh%u\n", m);
		WIZ_EMIT ("usemtl wiz0\n");
		for (uint i = 0; i < npos && ok; i++)
			WIZ_EMIT ("v %g %g %g\n", wiz_f (raw + R + offp + 12 * i), wiz_f (raw + R + offp + 12 * i + 4),
				wiz_f (raw + R + offp + 12 * i + 8));
		for (uint i = 0; i < ntex && ok; i++)
			WIZ_EMIT ("vt %g %g\n", wiz_f (raw + R + offt + 8 * i), 1.0f - wiz_f (raw + R + offt + 8 * i + 4));
		size_t p = 0;
		uint sgi = 0;
		while (ok)
		{
			if (!p || !raw[p])
			{
				// next segment (the scan found them: one or more commands up to a 0 byte)
				if (sgi >= nseg)
					break;
				p = segs[sgi++][0];
			}
			const uint cmd = raw[p];
			const uint cnt = rd_be16 (raw + p + 1);
			p += 3;
			if ((cmd != 0x80 && cmd != 0x90 && cmd != 0x98 && cmd != 0xa0) || !wiz_ok (raw, sz, p, (size_t)cnt * stride))
			{
				ok = false;
				break;
			}
			uint pi[65536 + 4], ti[65536 + 4];
			for (uint i = 0; i < cnt; i++)
			{
				const u8 *v = raw + p + (size_t)i * stride;
				uint vals[4] = { 0, 0, 0, 0 }, q = 0;
				for (uint k = 0; k < 4; k++)
					if (pres[k])
					{
						vals[k] = md[k] == 3 ? rd_be16 (v + q) : v[q];
						q += md[k] == 3 ? 2 : 1;
					}
				pi[i] = vals[0];
				ti[i] = offt ? vals[3] : 0;
				if (pi[i] >= npos || (offt && ti[i] >= ntex))
					ok = false;
			}
			p += (size_t)cnt * stride;
			if (!ok)
				break;
#define WIZ_TRI(a, b, c) \
	do { \
		if (offt) \
			WIZ_EMIT ("f %u/%u %u/%u %u/%u\n", vbase + pi[a], tbase + ti[a], vbase + pi[b], tbase + ti[b], vbase + pi[c], tbase + ti[c]); \
		else \
			WIZ_EMIT ("f %u %u %u\n", vbase + pi[a], vbase + pi[b], vbase + pi[c]); \
		ntri++; \
	} while (0)
			if (cmd == 0x90)
				for (uint i = 0; i + 2 < cnt; i += 3)
					WIZ_TRI (i, i + 1, i + 2);
			else if (cmd == 0x98)
				for (uint i = 0; i + 2 < cnt; i++)
				{
					if (i & 1)
						WIZ_TRI (i + 1, i, i + 2);
					else
						WIZ_TRI (i, i + 1, i + 2);
				}
			else if (cmd == 0xa0)
				for (uint i = 1; i + 1 < cnt; i++)
					WIZ_TRI (0, i, i + 1);
			else
				for (uint i = 0; i + 3 < cnt; i += 4)
				{
					WIZ_TRI (i, i + 1, i + 2);
					WIZ_TRI (i, i + 2, i + 3);
				}
		}
		vbase += npos;
		tbase += ntex;
		nmesh++;
	}
	if (!ok || !ntri)
	{
		FREE (obj);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT WIZ:%s (%u meshes, %u triangles, %u records skipped) -> %s/model.obj\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, nmesh, ntri, nskip, dest);
	enumError err = ERR_OK;
	if (!testmode)
	{
		// textures
		uint ntx = 0;
		if (wiz_ok (raw, sz, blob, 8) && rd_be32 (raw + blob) <= 64)
		{
			const uint cnt = rd_be32 (raw + blob);
			for (uint i = 0; i < cnt && wiz_ok (raw, sz, blob + 8 + 4 * (size_t)i, 4); i++)
			{
				const size_t e = rd_be32 (raw + blob + 8 + 4 * i);
				if (!wiz_ok (raw, sz, e, 16))
					continue;
				const size_t doff = rd_be32 (raw + e);
				const uint w = rd_be16 (raw + e + 4), h = rd_be16 (raw + e + 6), fmt = rd_be32 (raw + e + 8);
				if (!w || !h || w > 2048 || h > 2048 || fmt > 14 || fmt == 7 || fmt == 10 || fmt == 11 || fmt == 12 || fmt == 13)
					continue;
				const uint bw = fmt == 0 || fmt == 1 || fmt == 2 || fmt == 14 ? 8 : 4;
				const uint bh = fmt == 0 || fmt == 14 ? 8 : 4;
				const uint pw = (w + bw - 1) / bw * bw, ph = (h + bh - 1) / bh * bh;
				const uint bits = fmt == 0 || fmt == 14 ? 4 : fmt == 1 || fmt == 2 ? 8 : fmt == 6 ? 32 : 16;
				const size_t need = (size_t)pw * ph * bits / 8;
				if (fmt == 8 || fmt == 9 || !wiz_ok (raw, sz, doff, need))
					continue;
				u8 *rgba = 0;
				if (DecodeGXTexture_RGBA (&rgba, pw, ph, fmt, raw + doff, need, 0, 0, 0) || !rgba)
				{
					FREE (rgba);
					continue;
				}
				if (pw != w)
					for (uint y = 1; y < h; y++)
						memmove (rgba + (size_t)y * w * 4, rgba + (size_t)y * pw * 4, (size_t)w * 4);
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%stex_%04u.png", dest, basedir ? basedir : "", ntx);
				if (!SaveDecodedRGBAToPNG (rgba, w, h, &be_func, path, 0, true))
					ntx++;
			}
		}
		char head[128];
		const int hl = snprintf (head, sizeof (head), "%s", ntx == 1 ? "mtllib model.mtl\n" : "");
		char *full = MALLOC (used + hl + 1);
		if (full)
		{
			memcpy (full, head, hl);
			memcpy (full + hl, obj, used);
			err = shade_write (arg, dest, basedir, "model.obj", (u8 *)full, (uint)(used + hl));
			FREE (full);
		}
		if (!err && ntx == 1)
		{
			static const char mtl[] = "newmtl wiz0\nKd 1 1 1\nmap_Kd tex_0000.png\n";
			err = shade_write (arg, dest, basedir, "model.mtl", (const u8 *)mtl, sizeof (mtl) - 1);
		}
	}
	FREE (obj);
	FREE (raw);
	return err;
}
