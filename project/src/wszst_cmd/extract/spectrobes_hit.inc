// Spectrobes: Origins (Wii) collision geometry (".HIT", big endian, magic 0f0f0f0f). Layout: {magic, u32 table count T, u32 flags, u32 offset of the group data}, then T table records {u32 group
// count, u32 offset of the groups, u32 id, 0} at 0x10. A group is 0x30 bytes {char name[0x20], u32 record count, u32 first record offset, u32 end of the section after the records, u32 id}. The wall
// and ground groups hold 0x90 byte quad records {4 floats (0.5, 0.5, 0.5, 1.0), 3x4 floats {A.x, A.y, A.z, D.x}, {B.x, B.y, B.z, D.y}, {C.x, C.y, C.z, D.z} (the quad corners A, B, C, D in order),
// 5 plane rows {nx, ny, nz, w} for the quad and its four edges}; other groups (0x60 byte object nodes of the small .HIT files) are skipped. Written as
// "<file>.d/collision.obj" (one object per group, one quad per record) and "<file>.d/groups.json" (names, counts).
static enumError extract_spectrobes_hit_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".hit") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x40 || rd_be32 (raw) != 0x0f0f0f0f)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 nt = rd_be32 (raw + 4);
	if (!nt || nt > 64 || 0x10 + 16ull * nt > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	njm_out_t o = { MALLOC (1 << 20), 0, 1 << 20 };
	sumo_buf_t j = { 0, 0, 0, true };
	sumo_printf (&j, "{\n\t\"format\": \"Spectrobes HIT\",\n\t\"groups\": [");
	uint ngr = 0, nquads = 0, vbase = 1;
	for (u32 t = 0; t < nt && o.b; t++)
	{
		const u32 ng = rd_be32 (raw + 0x10 + 16 * t), go = rd_be32 (raw + 0x14 + 16 * t);
		if (!ng || ng > 4096 || (u64)go + 0x30ull * ng > sz)
			continue;
		for (u32 g = 0; g < ng && o.b; g++)
		{
			const u8 *r = raw + go + 0x30 * g;
			const u32 cnt = rd_be32 (r + 0x20), off = rd_be32 (r + 0x24);
			if (!cnt || cnt > 0x400000 || (u64)off + 0x90ull * cnt > sz || !memchr (r, 0, 0x20))
				continue;
			// plausibility: every record starts with the 0.5, 0.5, 0.5, 1.0 floats
			bool ok = true;
			for (u32 k = 0; k < cnt && ok; k++)
				ok = rd_be32 (raw + off + 0x90ull * k + 12) == 0x3f800000u;
			if (!ok)
				continue;
			sumo_printf (&j, "%s\n\t\t{\"name\": \"%s\", \"quads\": %u}", ngr ? "," : "", (ccp)r, cnt);
			njm_put (&o, "o %s\n", (ccp)r);
			for (u32 k = 0; k < cnt; k++)
			{
				const u8 *q = raw + off + 0x90ull * k + 0x10;
				float m[12];
				for (uint i = 0; i < 12; i++)
				{
					union
					{
						u32 u;
						float f;
					} x = { rd_be32 (q + 4 * i) };
					m[i] = x.f;
				}
				njm_put (&o, "v %g %g %g\nv %g %g %g\nv %g %g %g\nv %g %g %g\n", m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10], m[3], m[7], m[11]);
				njm_put (&o, "f %u %u %u %u\n", vbase, vbase + 1, vbase + 2, vbase + 3);
				vbase += 4;
				nquads++;
			}
			ngr++;
		}
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (o.b && ngr)
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE SPECTROBES-HIT:%s (%u groups, %u quads) -> %s/collision.obj\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ngr, nquads, dest);
		err = testmode ? ERR_OK : shade_write (arg, dest, basedir, "collision.obj", (u8 *)o.b, (uint)o.used);
		if (!err && !testmode)
		{
			sumo_printf (&j, "\n\t]\n}\n");
			if (j.ok && j.n && j.n < 0xffffffffu)
				err = shade_write (arg, dest, basedir, "groups.json", (u8 *)j.p, (uint)j.n);
		}
	}
	FREE (j.p);
	FREE (o.b);
	FREE (raw);
	return err;
}
