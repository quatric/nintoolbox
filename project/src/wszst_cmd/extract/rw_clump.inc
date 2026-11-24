// Criterion RenderWare clumps (".rws", ".dff"; stock little-endian chunks, e.g. Rebel Raiders: Operation Nighthawk objects): chunk 0x10 (Clump) -> 0x1a (GeometryList) -> 0xf
// (Geometry) whose struct (0x01) carries flags, triangle/vertex/morph-target counts, optional prelit colours and UV sets, the triangle list {v2, v1, material, v3} and the
// morph target's positions and normals in plain form (version >= 3.4 layout). Written as one Wavefront "<name>.obj" (a group per geometry, vertex positions, UVs, normals,
// triangles with material groups); frame transforms are not applied.
// Chunk walk: collect geometry (0xf) and world sector (0x9) chunk offsets below [o, end); descends into chunk groups 0x29, clumps 0x10, geometry lists 0x1a,
// worlds 0xb and plane sectors 0xa; wfl[] gets the enclosing world's format flags (struct +0x24). A TOC (0x24), textures (0x23) and animations (0x1b) are skipped.
static void rw_find_geometry (const u8 *raw, size_t o, size_t end, uint level, u32 flags, size_t *geo, u32 *wfl, uint *ng)
{
	while (o + 12 <= end && *ng < 4096 && level < 64)
	{
		const u32 t = rd_le32 (raw + o), s = rd_le32 (raw + o + 4);
		if (s > end - o - 12)
			return;
		if (t == 0xf || t == 9)
		{
			wfl[*ng] = flags;
			geo[(*ng)++] = o;
		}
		else if (t == 0xb && s >= 24 + 0x28 && rd_le32 (raw + o + 12) == 1)
			rw_find_geometry (raw, o + 12, o + 12 + s, level + 1, rd_le32 (raw + o + 24 + 0x24), geo, wfl, ng);
		else if (t == 0x29 || t == 0x10 || t == 0x1a || t == 0xa)
			rw_find_geometry (raw, o + 12, o + 12 + s, level + 1, flags, geo, wfl, ng);
		o += 12 + s;
	}
}

// World sector (0x9) struct: {u32 material base, u32 triangles, u32 vertices, bbox[6], u32, u32}, then positions, packed normals (u32, world flag 0x10), prelit colours
// (u32, flag 8), UV sets (8 bytes each; count in flags bits 16-23, else 1 for flag 4, 2 for 0x80) and triangles {u16 v0, v1, v2, material}.
static bool rw_sector_obj (const u8 *raw, size_t at, size_t send, u32 fl, char *txt, size_t cap, size_t *o, uint *voff, uint g)
{
	const u8 *b = raw + at + 24;
	const size_t ssz = rd_le32 (raw + at + 16);
	if (ssz < 44 || at + 24 + ssz > send)
		return false;
	const uint mb = rd_le32 (b), nt = rd_le32 (b + 4), nv = rd_le32 (b + 8);
	if (!nv || !nt || nv > 65536 || nt > 400000)
		return false;
	const uint nuv = (fl & 4) || (fl & 0x80) ? (((fl >> 16) & 0xff) ? ((fl >> 16) & 0xff) : (fl & 0x80) ? 2 : 1) : 0;
	const size_t uvq = 44 + (size_t)nv * (12 + ((fl & 0x10) ? 4 : 0) + ((fl & 8) ? 4 : 0)), triq = uvq + (size_t)nv * 8 * nuv;
	if (fl & 0x1000000 || triq + (size_t)nt * 8 != ssz || *o + (size_t)nv * 100 + (size_t)nt * 40 + 256 > cap)
		return false;
	*o += (size_t)sprintf (txt + *o, "g sector%u\n", g);
	for (uint i = 0; i < nv; i++)
	{
		float v[3];
		for (int k = 0; k < 3; k++) { const u32 u = rd_le32 (b + 44 + 12 * i + 4 * k); memcpy (&v[k], &u, 4); }
		*o += (size_t)sprintf (txt + *o, "v %g %g %g\n", v[0], v[1], v[2]);
	}
	if (nuv)
		for (uint i = 0; i < nv; i++)
		{
			float v[2];
			for (int k = 0; k < 2; k++) { const u32 u = rd_le32 (b + uvq + 8 * i + 4 * k); memcpy (&v[k], &u, 4); }
			*o += (size_t)sprintf (txt + *o, "vt %g %g\n", v[0], 1.0f - v[1]);
		}
	uint lastm = 0xffffffff;
	for (uint i = 0; i < nt; i++)
	{
		const u8 *tr = b + triq + 8 * i;
		const uint a = rd_le16 (tr), c = rd_le16 (tr + 2), d = rd_le16 (tr + 4), m = mb + rd_le16 (tr + 6);
		if (a >= nv || c >= nv || d >= nv)
			continue;
		if (m != lastm)
			*o += (size_t)sprintf (txt + *o, "usemtl mat%u\n", lastm = m);
		if (nuv)
			*o += (size_t)sprintf (txt + *o, "f %u/%u %u/%u %u/%u\n", *voff + a, *voff + a, *voff + c, *voff + c, *voff + d, *voff + d);
		else
			*o += (size_t)sprintf (txt + *o, "f %u %u %u\n", *voff + a, *voff + c, *voff + d);
	}
	*voff += nv;
	return true;
}

// Platform-independent texture dictionary (0x23, RenderWare 3.5+; Aliens in the Attic models and Textures/*.RWS): {u16 count, u16 device}, then per texture {u32 levels,
// levels x image chunk 0x18 {struct {u32 width, height, depth, stride}, height x stride pixel bytes (RGBA8 at depth 32, palette indices at 4/8), then 1 << depth RGBA
// palette entries}, texture chunk 0x6 {struct, string name, string mask, extension}}. The top mip level of each is written as "<file>.d/<name>.png".
static uint rw_pitxd_export (const u8 *raw, size_t sz, ccp arg)
{
	uint wrote = 0;
	size_t o = 0;
	while (o + 12 <= sz)
	{
		const u32 t = rd_le32 (raw + o), s = rd_le32 (raw + o + 4);
		if (s > sz - o - 12)
			break;
		if (t != 0x23 || s < 4)
		{
			o += 12 + s;
			continue;
		}
		const size_t end = o + 12 + s;
		size_t p = o + 16;
		const uint n = rd_le16 (raw + o + 12);
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		for (uint i = 0; i < n && p + 4 <= end; i++)
		{
			const uint nl = rd_le32 (raw + p);
			p += 4;
			const u8 *im = 0;
			for (uint l = 0; l < nl && p + 12 <= end; l++)
			{
				const u32 is = rd_le32 (raw + p + 4);
				if (rd_le32 (raw + p) != 0x18 || is > end - p - 12)
					return wrote;
				if (!l)
					im = raw + p;
				p += 12 + is;
			}
			if (p + 12 > end || rd_le32 (raw + p) != 6 || rd_le32 (raw + p + 4) > end - p - 12)
				return wrote;
			const size_t tex = p;
			p += 12 + rd_le32 (raw + p + 4);
			char name[64] = "";
			const size_t sp = tex + 12 + 12 + rd_le32 (raw + tex + 16); // name string after the texture struct
			if (sp + 12 <= p && rd_le32 (raw + sp) == 2 && rd_le32 (raw + sp + 4) <= p - sp - 12)
				snprintf (name, sizeof (name), "%.*s", (int)rd_le32 (raw + sp + 4), (ccp)raw + sp + 12);
			for (char *c = name; *c; c++)
				if (!isalnum ((u8)*c) && *c != '_' && *c != '-' && *c != '.')
					*c = '_';
			if (!im || rd_le32 (im + 12) != 1 || rd_le32 (im + 16) < 16)
				continue;
			const uint w = rd_le32 (im + 24), h = rd_le32 (im + 28), depth = rd_le32 (im + 32), stride = rd_le32 (im + 36);
			const size_t isz = rd_le32 (im + 4) - 28, npal = depth <= 8 ? (size_t)1 << depth : 0;
			if (!w || !h || w > 4096 || h > 4096 || (depth != 4 && depth != 8 && depth != 32) || stride < (w * depth + 7) / 8
				|| (size_t)h * stride + npal * 4 > isz)
				continue;
			const u8 *px = im + 40, *pal = px + (size_t)h * stride;
			const uint xw = EXPAND8 (w), xh = EXPAND8 (h);
			u8 *rgba = CALLOC (1, (size_t)xw * xh * 4);
			for (uint y = 0; y < h; y++)
				for (uint x = 0; x < w; x++)
				{
					const u8 *row = px + (size_t)y * stride;
					const u8 *c = depth == 32 ? row + 4 * x : pal + 4 * (depth == 8 ? row[x] : x & 1 ? row[x / 2] >> 4 : row[x / 2] & 15);
					memcpy (rgba + ((size_t)y * xw + x) * 4, c, 4);
				}
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s%03u.png", dest, name, *name ? "_" : "", i);
			if (!wrote)
				CreatePath (dest, true);
			Image_t img;
			InitializeIMG (&img);
			img.data = rgba;
			img.data_alloced = true;
			img.data_size = (size_t)xw * xh * 4;
			img.width = w;
			img.xwidth = xw;
			img.height = h;
			img.xheight = xh;
			img.iform = img.info_iform = IMG_X_RGB;
			img.info_fform = FF_PNG;
			img.info_n_image = 1;
			img.endian = &le_func;
			if (!SavePNG (&img, false, 0, path, 0, 0, true, 0))
				wrote++;
			ResetIMG (&img);
		}
		o = end;
	}
	if (wrote && verbose >= 0)
		fprintf (stdlog, "%sEXTRACT RW-PITXD:%s (%u textures)\n", verbose > 0 ? "\n" : "", arg, wrote);
	return wrote;
}

static enumError extract_rw_clump_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	(void)basedir;
	if (!is_ext (arg, ".rws") && !is_ext (arg, ".dff"))
		return ERR_NOTHING_TO_DO;
	// Aliens in the Attic puts a TOC (0x24) and a PI texture dictionary (0x23) before the clump
	if (!file_has_magic (arg, "\x10\0\0\0", 4) && !file_has_magic (arg, "\x24\0\0\0", 4) && !file_has_magic (arg, "\x23\0\0\0", 4)
		&& !file_has_magic (arg, "\x29\0\0\0", 4) && !file_has_magic (arg, "\x2b\0\0\0", 4))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	// geometries (0xf) and world sectors (0x9) of every clump and world, also inside chunk groups (0x29)
	size_t geo[4096];
	u32 wfl[4096];
	uint ng = 0;
	rw_find_geometry (raw, 0, sz, 0, 0, geo, wfl, &ng);
	const uint ntex = testmode ? 0 : rw_pitxd_export (raw, sz, arg);
	size_t cap = 4096;
	for (uint g = 0; g < ng; g++)
		cap += (size_t)rd_le32 (raw + geo[g] + 4) * 12 + 4096;
	char *txt = ng ? CALLOC (cap, 1) : 0;
	size_t o = 0;
	uint voff = 1, wrote = 0;
	for (uint g = 0; txt && g < ng; g++)
	{
		const u8 *st = raw + geo[g] + 12;
		const size_t send = geo[g] + 12 + rd_le32 (raw + geo[g] + 4);
		if (st + 12 > raw + sz || rd_le32 (st) != 1)
			continue;
		if (rd_le32 (raw + geo[g]) == 9)
		{
			if (rw_sector_obj (raw, geo[g], send, wfl[g], txt, cap, &o, &voff, g))
				wrote++;
			continue;
		}
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
			else if (nuv)
				o += (size_t)sprintf (txt + o, "f %u/%u %u/%u %u/%u\n", a, a, c, c, d3, d3);
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
	if (!wrote && ntex)
		err = ERR_OK;
	FREE (txt);
	FREE (raw);
	return err;
}
