// IO Interactive Glacier model containers (Mini Ninjas ".prm" inside the scene ".zip" packs; big
// endian). Header {u32 table offset, u32 chunk count, u32 table offset, 0}; the table at the end
// holds one {u32 offset, u32 size, u32, 0} entry per chunk (chunk 0 is the header itself). Chunks
// are typed objects (mesh nodes, material instances, an embedded loader XML, ...). The geometry is
// reached through 16-byte mesh descriptors {u32 vertex count, u32 vertex buffer chunk, u16
// material, u16 flags, u32 index buffer chunk}: the index buffer is {u16 1, u16 count, count x u16}
// (triangle list) and the vertex buffer holds 20-byte vertices {f32 x, y, z, u8 0, s8 normal x, y,
// z (/127), s16 u, v (/512)} (flags 0 buffers carry 2 more bytes per vertex after the array). A
// descriptor is accepted only if the largest index is exactly vertex count - 1 and both buffers fit
// their chunks. Meshes are in object space (instance transforms live in the scene files) and are
// written as one object each
// ("mesh_<chunk>_m<material>") to "<file>.d/model.obj". 36,000+ meshes over the 59 retail files.
static enumError extract_glacier_prm_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".prm") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x20)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 table = rd_be32 (raw), n = rd_be32 (raw + 4);
	if (rd_be32 (raw + 8) != table || rd_be32 (raw + 12) || !n || n > 1000000
		|| (u64)table + 16ull * n != sz || rd_be32 (raw + table)
		|| rd_be32 (raw + table + 4) != 0x10)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u8 *tab = raw + table;
	for (u32 i = 0; i < n; i++)
		if ((u64)rd_be32 (tab + 16 * i) + rd_be32 (tab + 16 * i + 4) > table)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	njm_out_t o = { MALLOC (1 << 20), 0, 1 << 20 };
	uint meshes = 0, base = 1;
	for (u32 i = 1; i < n && o.b; i++)
	{
		if (rd_be32 (tab + 16 * i + 4) != 0x10)
			continue;
		const u8 *d = raw + rd_be32 (tab + 16 * i);
		const u32 nv = rd_be32 (d), vb = rd_be32 (d + 4), ib = rd_be32 (d + 12);
		const uint mat = rd_be16 (d + 8);
		if (!nv || nv > 65536 || vb >= n || ib >= n || vb == ib)
			continue;
		const u8 *iv = raw + rd_be32 (tab + 16 * ib), *vv = raw + rd_be32 (tab + 16 * vb);
		const u32 isz = rd_be32 (tab + 16 * ib + 4), vsz = rd_be32 (tab + 16 * vb + 4);
		if (isz < 4 || rd_be16 (iv) != 1)
			continue;
		const uint cnt = rd_be16 (iv + 2);
		if (!cnt || cnt % 3 || 4 + 2 * (size_t)cnt > isz || isz > 4 + 2 * (size_t)cnt + 15
			|| vsz < 20 * (u64)nv)
			continue;
		uint mx = 0;
		for (uint k = 0; k < cnt; k++)
			if (rd_be16 (iv + 4 + 2 * k) > mx)
				mx = rd_be16 (iv + 4 + 2 * k);
		if (mx != nv - 1)
			continue;
		njm_put (&o, "o mesh_%u_m%u\n", i, mat);
		for (u32 k = 0; k < nv; k++)
		{
			const u8 *v = vv + 20 * (size_t)k;
			union
			{
				u32 u;
				float f;
			} x = { rd_be32 (v) }, y = { rd_be32 (v + 4) }, z = { rd_be32 (v + 8) };
			njm_put (&o, "v %g %g %g\n", x.f, y.f, z.f);
		}
		for (u32 k = 0; k < nv; k++)
		{
			const u8 *v = vv + 20 * (size_t)k;
			njm_put (&o, "vt %g %g\n", (s16)rd_be16 (v + 16) / 512.0,
				1.0 - (s16)rd_be16 (v + 18) / 512.0);
		}
		for (u32 k = 0; k < nv; k++)
		{
			const u8 *v = vv + 20 * (size_t)k;
			njm_put (&o, "vn %g %g %g\n", (s8)v[13] / 127.0, (s8)v[14] / 127.0, (s8)v[15] / 127.0);
		}
		for (uint k = 0; k < cnt; k += 3)
		{
			const uint a = rd_be16 (iv + 4 + 2 * k) + base, b = rd_be16 (iv + 6 + 2 * k) + base,
					   c = rd_be16 (iv + 8 + 2 * k) + base;
			njm_put (&o, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", a, a, a, b, b, b, c, c, c);
		}
		base += nv;
		meshes++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (o.b && meshes)
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE GLACIER-PRM:%s (%u meshes) -> %s/model.obj\n",
				verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, meshes, dest);
		err = testmode ? ERR_OK
					   : shade_write (arg, dest, basedir, "model.obj", (u8 *)o.b, (uint)o.used);
	}
	else if (o.b)
		err = ERR_OK; // a valid container without plain meshes
	FREE (o.b);
	FREE (raw);
	return err;
}
