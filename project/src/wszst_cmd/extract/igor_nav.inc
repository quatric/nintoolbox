// Igor: The Game navigation meshes (".nav", Exient "chkf" container of type "nav"; big endian; the ".nad" text next to each file is the game's own dump of the same data). After the
// "chkf" header and the 9 byte string table "nav\0data\0" the data is {u32 0, 1, 4, 0, u32 data size, u32 vertex count V, u32 triangle count T}, V vertex records {f32 x, y, z, 16 bytes (0,
// 1.0, 0, 0), u32 n, n x u32 index of a triangle using the vertex} and T triangle records {3 x u32 vertex, 3 x s32 neighbouring triangle (-1 = none), u32 0}. Written as "navmesh.obj" (37 files,
// vertex and triangle counts equal the ones of the .nad dumps).
static enumError extract_igor_nav_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".nav") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x40 || memcmp (raw, "chkf", 4) || memcmp (raw + 8, "nav", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 sl = rd_be32 (raw + 4);
	size_t p = 8 + (size_t)sl;
	if (sl < 8 || p + 28 > sz || rd_be32 (raw + p + 4) != 1 || rd_be32 (raw + p + 8) != 4)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 nv = rd_be32 (raw + p + 20), nt = rd_be32 (raw + p + 24);
	p += 28;
	bool ok = nv && nt && nv <= 1000000 && nt <= 1000000;
	const size_t vstart = p;
	for (u32 i = 0; ok && i < nv; i++)
	{
		if (p + 32 > sz)
		{
			ok = false;
			break;
		}
		const u32 n = rd_be32 (raw + p + 28);
		if (n > nt || p + 32 + 4ull * n > sz)
		{
			ok = false;
			break;
		}
		p += 32 + 4 * (size_t)n;
	}
	const size_t tstart = p;
	ok = ok && tstart + 28ull * nt == sz;
	for (u32 i = 0; ok && i < nt; i++)
		for (uint k = 0; k < 3; k++)
			ok = ok && rd_be32 (raw + tstart + 28 * (size_t)i + 4 * k) < nv;
	if (!ok)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	njm_out_t o = { MALLOC (1 << 16), 0, 1 << 16 };
	njm_put (&o, "o navmesh\n");
	p = vstart;
	for (u32 i = 0; i < nv && o.b; i++)
	{
		union
		{
			u32 u;
			float f;
		} x = { rd_be32 (raw + p) }, y = { rd_be32 (raw + p + 4) }, z = { rd_be32 (raw + p + 8) };
		njm_put (&o, "v %g %g %g\n", x.f, y.f, z.f);
		p += 32 + 4 * (size_t)rd_be32 (raw + p + 28);
	}
	for (u32 i = 0; i < nt && o.b; i++)
	{
		const u8 *t = raw + tstart + 28 * (size_t)i;
		njm_put (&o, "f %u %u %u\n", rd_be32 (t) + 1, rd_be32 (t + 4) + 1, rd_be32 (t + 8) + 1);
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (o.b)
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE IGOR-NAV:%s (%u vertices, %u triangles) -> %s/navmesh.obj\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nv, nt, dest);
		err = testmode ? ERR_OK : shade_write (arg, dest, basedir, "navmesh.obj", (u8 *)o.b, (uint)o.used);
	}
	FREE (o.b);
	FREE (raw);
	return err;
}
