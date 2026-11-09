// Grasshopper Manufacture "GMF2" model (No More Heroes 1/2 ".gm2", and the ".gmf2" members of RMHG containers; layout after the public
// "GHM File Docs" notes): little endian header {"GMF2", u32 2, 16 zero bytes, s16 objects, textures, unused, materials, u32 offsets of the
// object / texture / unused / material tables}. Objects are 0x80 bytes {char name[8], flags, vertex buffer, parent, first child, prev, next,
// surface list, ..., s32 vertex divisor exponent (+0x2c), float position[3] (+0x30), scale[3] (+0x50)}; the shared vertex buffer holds big endian
// s16 xyz (divided by 2^exponent) or, for exponent -1, float xyz. Surfaces (32 bytes, linked) point to material and data: big endian
// {u32 length, u16 vertex count, ...} at +0, then triangle strips {u16 command (0x98 / 0x99), u16 count} of vertices {u16 index, s8 normal[3],
// [u16 colour, [u16 ?,]] s16 u, s16 v} (13, 11 or 9 bytes). Textures are embedded GCT0 files. Written as "model.obj" (positions accumulated down the parent
// chain, rotations are not applied), "model.mtl" and "<texture>.png".
typedef struct gmf2_t { const u8 *r; size_t n; } gmf2_t;

static bool gmf2_in (const gmf2_t *g, size_t o, size_t len)
{
	return o <= g->n && len <= g->n - o;
}

static float gmf2_f (const u8 *p)
{
	const u32 v = rd_le32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static float gmf2_bf (const u8 *p)
{
	const u32 v = rd_be32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static bool gmf2_strip_size (const gmf2_t *g, size_t p, uint nv, size_t end, uint vsz)
{
	uint got = 0;
	while (got < nv)
	{
		if (!gmf2_in (g, p, 4))
			return false;
		const uint cmd = rd_be16 (g->r + p), n = rd_be16 (g->r + p + 2);
		if ((cmd & 0xf8) != 0x98 && (cmd & 0xf8) != 0x90 && (cmd & 0xf8) != 0xa0)
			return false;
		p += 4 + (size_t)n * vsz;
		got += n;
		if (p > end)
			return false;
	}
	return got == nv;
}

static enumError extract_gmf2_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".gm2") && !is_ext (arg, ".gmf2"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	gmf2_t G = { raw, sz }, *g = &G;
	if (sz < 0x70 || memcmp (raw, "GMF2", 4))
		goto done;
	const uint no = rd_le16 (raw + 0x18), nt = rd_le16 (raw + 0x1a), nmat = rd_le16 (raw + 0x1e);
	const u32 oo = rd_le32 (raw + 0x20), ot = rd_le32 (raw + 0x24), om = rd_le32 (raw + 0x2c);
	if (!no || no > 4096 || nt > 256 || nmat > 1024 || !gmf2_in (g, oo, 0x80 * (size_t)no) || !gmf2_in (g, ot, 32 * (size_t)nt)
		|| !gmf2_in (g, om, 32 * (size_t)nmat))
		goto done;
	size_t cap = 1 << 16, used = 0;
	char *obj = MALLOC (cap);
	if (!obj)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	bool ok = true;
#define G_EMIT(...) \
	do { \
		for (;;) { \
			const int w_ = snprintf (obj + used, cap - used, __VA_ARGS__); \
			if (w_ >= 0 && (size_t)w_ < cap - used) { used += (size_t)w_; break; } \
			char *nb_ = REALLOC (obj, cap * 2); \
			if (!nb_) { ok = false; break; } \
			obj = nb_; cap *= 2; \
		} \
	} while (0)
	uint vbase = 1, tbase = 1, nbase = 1, ntri = 0, nmesh = 0;
	G_EMIT ("mtllib model.mtl\n");
	for (uint oi = 0; oi < no && ok; oi++)
	{
		const u8 *o = raw + oo + 0x80 * (size_t)oi;
		const u32 vb = rd_le32 (o + 12), sf0 = rd_le32 (o + 0x20);
		const int vdiv = (int)rd_le32 (o + 0x2c);
		if (!vb || !sf0 || vdiv < -1 || vdiv > 30)
			continue;
		// accumulated translation
		float tx = 0, ty = 0, tz = 0;
		{
			size_t c = oo + 0x80 * (size_t)oi;
			for (uint hop = 0; hop < 32 && c; hop++)
			{
				tx += gmf2_f (raw + c + 0x30), ty += gmf2_f (raw + c + 0x34), tz += gmf2_f (raw + c + 0x38);
				const u32 par = rd_le32 (raw + c + 16);
				if (!gmf2_in (g, par, 0x80))
					break;
				c = par;
			}
		}
		const float sx = gmf2_f (o + 0x50), sy = gmf2_f (o + 0x54), sz2 = gmf2_f (o + 0x58);
		// vertex count of the shared buffer = highest index used by the object's surfaces + 1
		uint vmax = 0;
		for (int pass = 0; pass < 2 && ok; pass++)
		{
			uint first_v = vbase;
			if (pass == 1)
			{
				if (!vmax)
					break;
				G_EMIT ("o %.8s\n", (ccp)o);
				for (uint k = 0; k < vmax; k++)
				{
					float x, y, z;
					const size_t p = vb + (size_t)k * (vdiv < 0 ? 12 : 6);
					if (!gmf2_in (g, p, vdiv < 0 ? 12 : 6))
						x = y = z = 0;
					else if (vdiv < 0)
						x = gmf2_bf (raw + p), y = gmf2_bf (raw + p + 4), z = gmf2_bf (raw + p + 8);
					else
					{
						const float d = (float)(1u << vdiv);
						x = (s16)rd_be16 (raw + p) / d, y = (s16)rd_be16 (raw + p + 2) / d, z = (s16)rd_be16 (raw + p + 4) / d;
					}
					G_EMIT ("v %.6f %.6f %.6f\n", x * sx + tx, y * sy + ty, z * sz2 + tz);
				}
				vbase += vmax;
				nmesh++;
			}
			u32 s = sf0;
			for (uint ns = 0; s && ns < 256 && gmf2_in (g, s, 32) && ok; ns++)
			{
				const u32 nx = rd_le32 (raw + s + 4), dat = rd_le32 (raw + s + 8), mat = rd_le32 (raw + s + 12);
				if (!gmf2_in (g, dat, 32))
					break;
				const u32 len = rd_be32 (raw + dat);
				const uint nvt = rd_be16 (raw + dat + 4);
				const size_t p0 = (size_t)dat + 32, end = (size_t)dat + 32 + len + 32;
				uint vsz = 0;
				if (gmf2_strip_size (g, p0, nvt, end, 11))
					vsz = 11;
				else if (gmf2_strip_size (g, p0, nvt, end, 9))
					vsz = 9;
				else if (gmf2_strip_size (g, p0, nvt, end, 13))
					vsz = 13;
				if (vsz)
				{
					if (pass == 1)
					{
						char mname[16] = "none";
						if (mat && gmf2_in (g, mat, 32))
							snprintf (mname, sizeof (mname), "%.8s", (ccp)raw + mat);
						G_EMIT ("usemtl m_%s\n", mname);
					}
					size_t p = p0;
					uint got = 0;
					while (got < nvt)
					{
						const uint cmd = rd_be16 (raw + p) & 0xf8, n = rd_be16 (raw + p + 2);
						p += 4;
						if (pass == 0)
						{
							for (uint k = 0; k < n; k++)
							{
								const uint idx = rd_be16 (raw + p + (size_t)k * vsz);
								if (idx + 1 > vmax)
									vmax = idx + 1;
							}
						}
						else
						{
							for (uint k = 0; k < n; k++)
							{
								const u8 *v = raw + p + (size_t)k * vsz;
								const int ux = (s16)rd_be16 (v + vsz - 4), vy = (s16)rd_be16 (v + vsz - 2);
								G_EMIT ("vt %.5f %.5f\n", ux / 1024.0, 1.0 - vy / 1024.0);
								G_EMIT ("vn %.4f %.4f %.4f\n", (s8)v[2] / 64.0, (s8)v[3] / 64.0, (s8)v[4] / 64.0);
							}
							for (uint k = 0; k + 2 < n || (cmd == 0x90 && k + 2 < n); k += cmd == 0x90 ? 3 : 1)
							{
								uint a = k, b = k + 1, c = k + 2;
								if (cmd == 0x98 && (k & 1))
									b = k + 2, c = k + 1;
								if (cmd == 0xa0)
									a = 0, b = k + 1, c = k + 2;
								const uint ia = rd_be16 (raw + p + (size_t)a * vsz), ib = rd_be16 (raw + p + (size_t)b * vsz),
									   ic = rd_be16 (raw + p + (size_t)c * vsz);
								G_EMIT ("f %u/%u/%u %u/%u/%u %u/%u/%u\n", first_v + ia, tbase + a, nbase + a, first_v + ib, tbase + b,
									nbase + b, first_v + ic, tbase + c, nbase + c);
								ntri++;
								if (cmd == 0x90 && k + 3 >= n)
									break;
							}
							tbase += n;
							nbase += n;
						}
						p += (size_t)n * vsz;
						got += n;
					}
				}
				s = nx;
			}
		}
	}
	if (!ok || !ntri)
	{
		FREE (obj);
		goto done;
	}
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT GMF2:%s (%u meshes, %u triangles) -> %s/model.obj\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, nmesh, ntri, dest);
		err = ERR_OK;
		if (!testmode)
		{
			// materials -> textures; textures decoded from the embedded GCT0
			char *mtl = MALLOC (nmat * 160 + 16);
			uint ml = 0;
			if (!mtl)
				err = ERR_OUT_OF_MEMORY;
			for (uint i = 0; i < nmat && !err; i++)
			{
				const u8 *m = raw + om + 32 * (size_t)i;
				const u32 md = rd_le32 (m + 0x14);
				char tname[16] = "";
				if (gmf2_in (g, md, 0x14))
				{
					const u32 th = rd_le32 (raw + md + 8);
					if (gmf2_in (g, th, 32))
						snprintf (tname, sizeof (tname), "%.8s", (ccp)raw + th);
				}
				ml += snprintf (mtl + ml, 160, "newmtl m_%.8s\nKd 1 1 1\n", (ccp)m);
				if (*tname)
					ml += snprintf (mtl + ml, 160, "map_Kd %s.png\n", tname);
			}
			for (uint i = 0; i < nt && !err; i++)
			{
				const u8 *t = raw + ot + 32 * (size_t)i;
				const u32 doff = rd_le32 (t + 0x10), dlen = rd_le32 (t + 0x18);
				if (!gmf2_in (g, doff, dlen) || dlen < 0x40 || memcmp (raw + doff, "GCT0", 4))
					continue;
				const uint fmt = rd_be32 (raw + doff + 4), w = rd_be16 (raw + doff + 8), h = rd_be16 (raw + doff + 10);
				const uint bw = fmt == 0 || fmt == 1 || fmt == 2 || fmt == 14 ? 8 : 4, bh = fmt == 0 || fmt == 14 ? 8 : 4;
				const uint bits = fmt == 0 || fmt == 14 ? 4 : fmt == 1 || fmt == 2 ? 8 : fmt == 6 ? 32 : 16;
				if (!w || !h || w > 2048 || h > 2048 || fmt > 14 || (fmt >= 7 && fmt != 14))
					continue;
				const uint pw = (w + bw - 1) / bw * bw, ph = (h + bh - 1) / bh * bh;
				const size_t need = (size_t)pw * ph * bits / 8;
				if (!gmf2_in (g, doff + 0x40, need))
					continue;
				u8 *rgba = 0;
				if (DecodeGXTexture_RGBA (&rgba, pw, ph, fmt, raw + doff + 0x40, need, 0, 0, 0) || !rgba)
				{
					FREE (rgba);
					continue;
				}
				if (pw != w)
					for (uint y = 1; y < h; y++)
						memmove (rgba + (size_t)y * w * 4, rgba + (size_t)y * pw * 4, (size_t)w * 4);
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%s%.8s.png", dest, basedir ? basedir : "", (ccp)t);
				SaveDecodedRGBAToPNG (rgba, w, h, &be_func, path, 0, true);
			}
			if (!err)
				err = shade_write (arg, dest, basedir, "model.obj", (u8 *)obj, (uint)used);
			if (!err)
				err = shade_write (arg, dest, basedir, "model.mtl", (u8 *)mtl, ml);
			FREE (mtl);
		}
	}
	FREE (obj);
done:
	FREE (raw);
	return err;
}
