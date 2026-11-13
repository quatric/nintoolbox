// Criterion RenderWare clumps (".rws", ".dff"; stock little-endian chunks, e.g. Rebel Raiders: Operation Nighthawk objects): chunk 0x10 (Clump) -> 0x1a (GeometryList) -> 0xf
// (Geometry) whose struct (0x01) carries flags, triangle/vertex/morph-target counts, optional prelit colours and UV sets, the triangle list {v2, v1, material, v3} and the
// morph target's positions and normals in plain form (version >= 3.4 layout). Written as one Wavefront "<name>.obj" (a group per geometry, vertex positions, UVs, normals,
// triangles with material groups); frame transforms are not applied.
static enumError extract_rw_clump_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	(void)basedir;
	if (!is_ext (arg, ".rws") && !is_ext (arg, ".dff"))
		return ERR_NOTHING_TO_DO;
	if (!file_has_magic (arg, "\x10\0\0\0", 4))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	// locate the geometry chunks (clump 0x10 -> geometry list 0x1a -> geometry 0xf)
	size_t geo[256];
	uint ng = 0;
	size_t p = 12;
	if (sz > 0x40 && rd_le32 (raw) == 0x10 && rd_le32 (raw + 4) + 12 <= sz)
	{
		const size_t cend = 12 + rd_le32 (raw + 4);
		while (p + 12 <= cend)
		{
			const uint t = rd_le32 (raw + p), s = rd_le32 (raw + p + 4);
			if (p + 12 + s > cend)
				break;
			if (t == 0x1a)
			{
				size_t q = p + 12;
				const size_t gend = p + 12 + s;
				while (q + 12 <= gend && ng < 256)
				{
					const uint gt = rd_le32 (raw + q), gs = rd_le32 (raw + q + 4);
					if (q + 12 + gs > gend)
						break;
					if (gt == 0xf)
						geo[ng++] = q;
					q += 12 + gs;
				}
			}
			p += 12 + s;
		}
	}
	size_t cap = 4096;
	for (uint g = 0; g < ng; g++)
		cap += rd_le32 (raw + geo[g] + 4) * 12 + 4096;
	char *txt = ng ? CALLOC (cap, 1) : 0;
	size_t o = 0;
	uint voff = 1, wrote = 0;
	for (uint g = 0; txt && g < ng; g++)
	{
		const u8 *st = raw + geo[g] + 12;
		const size_t send = geo[g] + 12 + rd_le32 (raw + geo[g] + 4);
		if (st + 12 > raw + sz || rd_le32 (st) != 1)
			continue;
		const size_t ssz = rd_le32 (st + 4);
		const u32 ver = rd_le32 (st + 8);
		const uint rv = ((ver >> 14) & 0x3ff00) | ((ver >> 16) & 0x3f) | 0x30000;
		const u8 *b = st + 12;
		if (st + 12 + ssz > raw + send || ssz < 16)
			continue;
		const uint fl = rd_le32 (b), nt = rd_le32 (b + 4), nv = rd_le32 (b + 8), nm = rd_le32 (b + 12);
		size_t q = 16 + (rv < 0x34000 ? 12 : 0);
		const uint nuv = (fl & 4) || (fl & 0x80) ? (((fl >> 16) & 0xff) ? ((fl >> 16) & 0xff) : (fl & 0x80) ? 2 : 1) : 0;
		if (fl & 0x1000000 || !nv || !nt || !nm || nv > 200000 || nt > 400000 || nm > 8)
			continue;
		if (fl & 8)
			q += (size_t)nv * 4;
		const size_t uvq = q;
		q += (size_t)nv * 8 * nuv;
		const size_t triq = q;
		q += (size_t)nt * 8;
		// first morph target
		if (q + 24 > ssz)
			continue;
		const uint hasv = rd_le32 (b + q + 16), hasn = rd_le32 (b + q + 20);
		const size_t vq = q + 24, nq = vq + (hasv ? (size_t)nv * 12 : 0);
		if (!hasv || nq + (hasn ? (size_t)nv * 12 : 0) > ssz)
			continue;
		if (o + (size_t)nv * 100 + (size_t)nt * 40 + 256 > cap)
			continue;
		o += (size_t)sprintf (txt + o, "g geometry%u\n", g);
		for (uint i = 0; i < nv; i++)
		{
			float v[3];
			memcpy (v, b + vq + 12 * i, 12);
			uint32_t u[3];
			memcpy (u, v, 12);
			for (int k = 0; k < 3; k++) { u[k] = rd_le32 ((u8 *)&u[k]); memcpy (&v[k], &u[k], 4); }
			o += (size_t)sprintf (txt + o, "v %g %g %g\n", v[0], v[1], v[2]);
		}
		if (nuv)
			for (uint i = 0; i < nv; i++)
			{
				uint32_t u[2];
				float v[2];
				memcpy (u, b + uvq + 8 * i, 8);
				for (int k = 0; k < 2; k++) { u[k] = rd_le32 ((u8 *)&u[k]); memcpy (&v[k], &u[k], 4); }
				o += (size_t)sprintf (txt + o, "vt %g %g\n", v[0], 1.0f - v[1]);
			}
		if (hasn)
			for (uint i = 0; i < nv; i++)
			{
				uint32_t u[3];
				float v[3];
				memcpy (u, b + nq + 12 * i, 12);
				for (int k = 0; k < 3; k++) { u[k] = rd_le32 ((u8 *)&u[k]); memcpy (&v[k], &u[k], 4); }
				o += (size_t)sprintf (txt + o, "vn %g %g %g\n", v[0], v[1], v[2]);
			}
		uint lastm = 0xffff;
		for (uint i = 0; i < nt; i++)
		{
			const u8 *tr = b + triq + 8 * i;
			const uint v2 = rd_le16 (tr), v1 = rd_le16 (tr + 2), mat = rd_le16 (tr + 4), v3 = rd_le16 (tr + 6);
			if (v1 >= nv || v2 >= nv || v3 >= nv)
				continue;
			if (mat != lastm)
			{
				o += (size_t)sprintf (txt + o, "usemtl mat%u\n", mat);
				lastm = mat;
			}
			const uint a = voff + v1, c = voff + v2, d3 = voff + v3;
			if (nuv && hasn)
				o += (size_t)sprintf (txt + o, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", a, a, a, c, c, c, d3, d3, d3);
			else if (hasn)
				o += (size_t)sprintf (txt + o, "f %u//%u %u//%u %u//%u\n", a, a, c, c, d3, d3);
			else
				o += (size_t)sprintf (txt + o, "f %u %u %u\n", a, c, d3);
		}
		voff += nv;
		wrote++;
	}
	if (wrote)
	{
		char dest[PATH_MAX];
		beside_source_dest_ext (dest, sizeof (dest), arg, ".obj");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT RW-CLUMP:%s (%u geometries) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, wrote, dest);
		if (testmode)
			err = ERR_OK;
		else
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f && fwrite (txt, 1, o, F.f) != o)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", o, dest);
			ResetFile (&F, opt_preserve);
		}
	}
	FREE (txt);
	FREE (raw);
	return err;
}
