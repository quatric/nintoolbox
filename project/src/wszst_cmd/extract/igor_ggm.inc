// Igor: The Game models (".ggm", Exient "chkf" container of type "xgm"; big endian). Layout: {"chkf", u32 N} then an N byte string table (type, section names, then every name and text
// matrix the file uses; all offsets below are relative to it). After the table {u32 version words..., u32 data size at +0x1c, u32 node count at +0x20, node records at +0x24}: one 20 byte
// record per skeleton node {u32 index, u32 parent (-1 = root), u32 name offset, u32 offset of the text of the local 4x4 matrix, u32 offset of the text of the inverse bind matrix}. The
// bulk data (vertices and indices of every mesh, end of the file, size = data size) follows the descriptors. A mesh descriptor is {u32 palette size, u32 vertex format name offset, u32
// vertex data offset, u32 vertex count, u32 stride (0x18), 0, u32 primitive name offset ("EPRIM_TRIANGLES"), u32 index count, u32 index data offset, 0, palette ...}: the vertices are
// {f32 x, y, z, s16 nx, ny, nz (/32768), u8 skin palette slot (format "I1") or two colour bytes ("C2"), 0, s16 u, v (/1024)}, the u16 triangle list follows its vertices, the palette
// lists {slot, node name offset} pairs for rigid skinning. The vertices are already in model space. Written as "<file>.d/model.obj" and "<file>.d/skeleton.json" (240 files).
static bool igor_ggm_text (const u8 *st, u32 sl, u32 off, double *m)
{
	if (off >= sl)
		return false;
	char tmp[512];
	size_t n = 0;
	while (off + n < sl && st[off + n] && n < sizeof (tmp) - 1)
	{
		tmp[n] = (char)st[off + n];
		n++;
	}
	tmp[n] = 0;
	char *p = tmp;
	for (uint k = 0; k < 16; k++)
	{
		char *e;
		m[k] = strtod (p, &e);
		if (e == p)
			return false;
		p = e;
	}
	return true;
}

static enumError extract_igor_ggm_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".ggm") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x40 || memcmp (raw, "chkf", 4) || memcmp (raw + 8, "xgm", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 sl = rd_be32 (raw + 4);
	const size_t hb = 8 + (size_t)sl;
	if (sl < 16 || hb + 0x28 > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u8 *st = raw + 8;
	const u32 dsz = rd_be32 (raw + hb + 0x1c), nn = rd_be32 (raw + hb + 0x20);
	if (dsz > sz - hb || nn > 4096 || hb + 0x24 + 20ull * nn > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t base = sz - dsz;
	// string offsets of the vertex format and primitive names
	u32 fmt[8], nfmt = 0, prim = 0;
	for (u32 i = 0; i + 16 < sl && nfmt < 8; i++)
		if (!memcmp (st + i, "EVTXSTREAMFMT", 13) && (!i || !st[i - 1]))
			fmt[nfmt++] = i;
	for (u32 i = 0; i + 15 < sl; i++)
		if (!memcmp (st + i, "EPRIM_TRIANGLES", 15) && (!i || !st[i - 1]))
		{
			prim = i;
			break;
		}
	njm_out_t o = { MALLOC (1 << 20), 0, 1 << 20 };
	uint meshes = 0, vbase = 1;
	for (size_t p = hb; nfmt && prim && p + 40 <= base && o.b; p += 4)
	{
		bool isf = false;
		for (u32 k = 0; k < nfmt; k++)
			isf |= rd_be32 (raw + p) == fmt[k];
		if (!isf || rd_be32 (raw + p + 12) != 0x18 || rd_be32 (raw + p + 16) || rd_be32 (raw + p + 20) != prim)
			continue;
		const u32 vo = rd_be32 (raw + p + 4), vn = rd_be32 (raw + p + 8), ic = rd_be32 (raw + p + 24), io = rd_be32 (raw + p + 28);
		if (!vn || vn > 65536 || !ic || ic % 3 || (u64)vo + 24ull * vn > io || (u64)io + 2ull * ic > dsz)
			continue;
		const u8 *vv = raw + base + vo, *iv = raw + base + io;
		bool ok = true;
		for (u32 k = 0; k < ic && ok; k++)
			ok = rd_be16 (iv + 2 * k) < vn;
		if (!ok)
			continue;
		njm_put (&o, "o mesh_%u\n", meshes);
		for (u32 k = 0; k < vn; k++)
		{
			union
			{
				u32 u;
				float f;
			} x = { rd_be32 (vv + 24 * k) }, y = { rd_be32 (vv + 24 * k + 4) }, z = { rd_be32 (vv + 24 * k + 8) };
			njm_put (&o, "v %g %g %g\n", x.f, y.f, z.f);
		}
		for (u32 k = 0; k < vn; k++)
			njm_put (&o, "vt %g %g\n", (s16)rd_be16 (vv + 24 * k + 20) / 1024.0, 1.0 - (s16)rd_be16 (vv + 24 * k + 22) / 1024.0);
		for (u32 k = 0; k < vn; k++)
			njm_put (&o, "vn %g %g %g\n", (s16)rd_be16 (vv + 24 * k + 12) / 32768.0, (s16)rd_be16 (vv + 24 * k + 14) / 32768.0, (s16)rd_be16 (vv + 24 * k + 16) / 32768.0);
		for (u32 k = 0; k < ic; k += 3)
		{
			const uint a = rd_be16 (iv + 2 * k) + vbase, b = rd_be16 (iv + 2 * k + 2) + vbase, c = rd_be16 (iv + 2 * k + 4) + vbase;
			njm_put (&o, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", a, a, a, b, b, b, c, c, c);
		}
		vbase += vn;
		meshes++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (o.b && meshes)
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE IGOR-GGM:%s (%u meshes) -> %s/model.obj\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, meshes, dest);
		err = testmode ? ERR_OK : shade_write (arg, dest, basedir, "model.obj", (u8 *)o.b, (uint)o.used);
		if (!err && !testmode && nn)
		{
			sumo_buf_t b = { 0, 0, 0, true };
			sumo_printf (&b, "{\n\t\"format\": \"Igor GGM skeleton\",\n\t\"nodes\": [");
			for (u32 i = 0; i < nn; i++)
			{
				const u8 *r = raw + hb + 0x24 + 20 * i;
				const u32 no = rd_be32 (r + 8);
				double m1[16], m2[16];
				if (rd_be32 (r) != i || no >= sl || !igor_ggm_text (st, sl, rd_be32 (r + 12), m1) || !igor_ggm_text (st, sl, rd_be32 (r + 16), m2))
					break;
				sumo_printf (&b, "%s\n\t\t{\"index\": %u, \"name\": \"%s\", \"parent\": %d, \"local\": [", i ? "," : "", i, (ccp)st + no, (int)rd_be32 (r + 4));
				for (uint k = 0; k < 16; k++)
					sumo_printf (&b, "%s%.9g", k ? ", " : "", m1[k]);
				sumo_printf (&b, "], \"inverse\": [");
				for (uint k = 0; k < 16; k++)
					sumo_printf (&b, "%s%.9g", k ? ", " : "", m2[k]);
				sumo_printf (&b, "]}");
			}
			sumo_printf (&b, "\n\t]\n}\n");
			if (b.ok && b.n && b.n < 0xffffffffu)
				err = shade_write (arg, dest, basedir, "skeleton.json", (u8 *)b.p, (uint)b.n);
			FREE (b.p);
		}
	}
	else if (o.b)
		err = ERR_OK;
	FREE (o.b);
	FREE (raw);
	return err;
}
