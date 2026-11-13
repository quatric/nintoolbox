// Sumo Digital "COG"/"GOG" geometry (".cog" + same-named ".gog": Virtua Tennis 2009; big endian). The .cog is an object graph with, per mesh, a block of 16-byte attribute descriptors
// {u8 attribute (3 position, 6 normal, 8/10 colour, 11 texcoord), u8 index width (1/2), 2 flag bytes, u8 component type (0 u8, 1 s8, 2 u16, 3 s16, 4 f32), u8 count code, u8 frac,
// u8, u8 stride, 3 pad, u32 offset into the .gog}, ended by a 0x0F descriptor, followed (aligned to 0x20) by a GX display list (0x80 quads, 0x90 triangles, 0x98 strip, 0xA0 fan; u16
// vertex count, then one u8/u16 index per descriptor in order). The .gog holds the vertex pools: positions as 3 x f32, normals as s8 x 3 (/64), texcoords as u16 x 2 (/512).
// Meshes whose positions are f32 triples are written to "<name>.cog.d/<name>.obj" as one group each, in the mesh's own (joint-local) space; node transforms and skinning are not applied.
typedef struct sumo_buf_t
{
	char *p;
	size_t n, cap;
	bool ok;
} sumo_buf_t;

static void sumo_printf (sumo_buf_t *b, const char *fmt, ...)
{
	char tmp[256];
	va_list ap;
	va_start (ap, fmt);
	const int l = vsnprintf (tmp, sizeof tmp, fmt, ap);
	va_end (ap);
	if (l <= 0 || (size_t)l >= sizeof tmp || !b->ok)
		return;
	if (b->n + (size_t)l + 1 > b->cap)
	{
		const size_t nc = b->cap ? b->cap * 2 : 1 << 16;
		char *q = MALLOC (nc);
		if (!q)
		{
			b->ok = false;
			return;
		}
		if (b->p)
			memcpy (q, b->p, b->n + 1);
		FREE (b->p);
		b->p = q;
		b->cap = nc;
	}
	memcpy (b->p + b->n, tmp, (size_t)l + 1);
	b->n += (size_t)l;
}

typedef struct sumo_desc_t
{
	uint attr, iw, ty, stride, off;
} sumo_desc_t;

static float sumo_be_float (const u8 *p)
{
	const uint v = rd_be32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static enumError extract_sumo_cog_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".cog"))
		return ERR_NOTHING_TO_DO;
	char gpath[PATH_MAX];
	snprintf (gpath, sizeof gpath, "%s", arg);
	strcpy (gpath + strlen (gpath) - 3, "gog");
	u8 *c = 0, *g = 0;
	size_t cs = 0, gs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x100 || LoadFileAlloc (gpath, 0, 0, &g, &gs, 0, 0, 0, false) || !g)
	{
		FREE (c);
		FREE (g);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	uint nmesh = 0, ntri = 0, nv = 0, nt = 0, nn = 0;
	for (size_t p = 0; p + 64 <= cs; p += 4)
	{
		if (c[p] != 3 || (c[p + 1] != 1 && c[p + 1] != 2) || c[p + 4] != 4 || c[p + 8] != 12)
			continue;
		sumo_desc_t ds[8];
		uint nd = 0;
		size_t q = p;
		bool ok = true;
		while (q + 16 <= cs && c[q] != 0x0f && ok)
		{
			if (nd >= 8 || c[q] > 0x10 || (c[q + 1] != 1 && c[q + 1] != 2))
			{
				ok = false;
				break;
			}
			ds[nd].attr = c[q];
			ds[nd].iw = c[q + 1];
			ds[nd].ty = c[q + 4];
			ds[nd].stride = c[q + 8];
			ds[nd].off = rd_be32 (c + q + 12);
			nd++;
			q += 16;
		}
		if (!ok || !nd || q + 16 > cs || rd_be32 (c + q) != 0x0f000000)
			continue;
		const size_t dl = (q + 16 + 31) & ~(size_t)31;
		uint stride = 0, ipos = 0, inrm = 99, iuv = 99;
		for (uint i = 0; i < nd; i++)
		{
			stride += ds[i].iw;
			if (ds[i].attr == 6 && ds[i].ty == 1 && ds[i].stride == 3 && inrm == 99)
				inrm = i;
			if (ds[i].attr == 11 && ds[i].ty == 2 && ds[i].stride == 4 && iuv == 99)
				iuv = i;
		}
		// walk the display list, collecting the highest index per descriptor
		uint mx[8] = { 0 };
		size_t e = dl;
		uint ncmd = 0;
		while (e + 3 <= cs && (c[e] == 0x80 || c[e] == 0x90 || c[e] == 0x98 || c[e] == 0xa0))
		{
			const uint cnt = rd_be16 (c + e + 1);
			if (!cnt || e + 3 + (size_t)cnt * stride > cs)
				break;
			for (uint i = 0; i < cnt; i++)
			{
				size_t o = e + 3 + (size_t)i * stride;
				for (uint d = 0; d < nd; d++)
				{
					const uint v = ds[d].iw == 1 ? c[o] : rd_be16 (c + o);
					o += ds[d].iw;
					if (v > mx[d])
						mx[d] = v;
				}
			}
			e += 3 + (size_t)cnt * stride;
			ncmd++;
		}
		if (!ncmd || (size_t)ds[ipos].off + ((size_t)mx[ipos] + 1) * 12 > gs)
			continue;
		if (inrm != 99 && (size_t)ds[inrm].off + ((size_t)mx[inrm] + 1) * 3 > gs)
			inrm = 99;
		if (iuv != 99 && (size_t)ds[iuv].off + ((size_t)mx[iuv] + 1) * 4 > gs)
			iuv = 99;
		sumo_printf (&b, "g mesh%u\n", nmesh);
		for (uint i = 0; i <= mx[ipos]; i++)
			sumo_printf (&b, "v %.6f %.6f %.6f\n", sumo_be_float (g + ds[ipos].off + 12 * i), sumo_be_float (g + ds[ipos].off + 12 * i + 4), sumo_be_float (g + ds[ipos].off + 12 * i + 8));
		if (iuv != 99)
			for (uint i = 0; i <= mx[iuv]; i++)
				sumo_printf (&b, "vt %.5f %.5f\n", rd_be16 (g + ds[iuv].off + 4 * i) / 512.0, 1.0 - rd_be16 (g + ds[iuv].off + 4 * i + 2) / 512.0);
		if (inrm != 99)
			for (uint i = 0; i <= mx[inrm]; i++)
				sumo_printf (&b, "vn %.4f %.4f %.4f\n", (signed char)g[ds[inrm].off + 3 * i] / 64.0, (signed char)g[ds[inrm].off + 3 * i + 1] / 64.0, (signed char)g[ds[inrm].off + 3 * i + 2] / 64.0);
		size_t pe = dl;
		while (pe < e)
		{
			const u8 cmd = c[pe];
			const uint cnt = rd_be16 (c + pe + 1);
			const u8 *v = c + pe + 3;
			pe += 3 + (size_t)cnt * stride;
			for (uint i = 0; i < cnt; i++)
			{
				uint ii[3];
				uint nt3 = 0;
				if (cmd == 0x90 && i % 3 == 2)
					ii[0] = i - 2, ii[1] = i - 1, ii[2] = i, nt3 = 1;
				else if (cmd == 0x98 && i >= 2)
				{
					ii[0] = (i & 1) ? i - 1 : i - 2;
					ii[1] = (i & 1) ? i - 2 : i - 1;
					ii[2] = i;
					nt3 = 1;
				}
				else if (cmd == 0xa0 && i >= 2)
					ii[0] = 0, ii[1] = i - 1, ii[2] = i, nt3 = 1;
				else if (cmd == 0x80 && i % 4 == 3)
				{
					// quad: two triangles, emitted one after the other
					for (uint h = 0; h < 2; h++)
					{
						const uint qi[3] = { i - 3, i - 3 + 1 + h, i - 3 + 2 + h };
						uint pv[3], tv[3], nvv[3];
						for (uint k = 0; k < 3; k++)
						{
							size_t o = (size_t)qi[k] * stride;
							uint idx[8];
							for (uint d = 0; d < nd; d++)
							{
								idx[d] = ds[d].iw == 1 ? v[o] : rd_be16 (v + o);
								o += ds[d].iw;
							}
							pv[k] = idx[ipos] + 1 + nv;
							tv[k] = iuv != 99 ? idx[iuv] + 1 + nt : 0;
							nvv[k] = inrm != 99 ? idx[inrm] + 1 + nn : 0;
						}
						sumo_printf (&b, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", pv[0], tv[0], nvv[0], pv[1], tv[1], nvv[1], pv[2], tv[2], nvv[2]);
						ntri++;
					}
				}
				if (nt3)
				{
					uint pv[3], tv[3], nvv[3];
					for (uint k = 0; k < 3; k++)
					{
						size_t o = (size_t)ii[k] * stride;
						uint idx[8];
						for (uint d = 0; d < nd; d++)
						{
							idx[d] = ds[d].iw == 1 ? v[o] : rd_be16 (v + o);
							o += ds[d].iw;
						}
						pv[k] = idx[ipos] + 1 + nv;
						tv[k] = iuv != 99 ? idx[iuv] + 1 + nt : 0;
						nvv[k] = inrm != 99 ? idx[inrm] + 1 + nn : 0;
					}
					if (pv[0] != pv[1] && pv[1] != pv[2] && pv[0] != pv[2])
					{
						sumo_printf (&b, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", pv[0], tv[0], nvv[0], pv[1], tv[1], nvv[1], pv[2], tv[2], nvv[2]);
						ntri++;
					}
				}
			}
		}
		nv += mx[ipos] + 1;
		nt += iuv != 99 ? mx[iuv] + 1 : 0;
		nn += inrm != 99 ? mx[inrm] + 1 : 0;
		nmesh++;
		p = e > p ? ((e - 1) & ~(size_t)3) : p;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (b.ok && b.n && ntri)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot) strcpy (dot, ".obj");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SUMO-COG:%s (%u meshes, %u triangles) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nmesh, ntri, dest, name);
		err = b.n < 0xffffffffu ? shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n) : ERR_NOTHING_TO_DO;
	}
	FREE (b.p);
	FREE (c);
	FREE (g);
	return err;
}
