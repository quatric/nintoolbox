// SEGA "GRO3" model containers (Sakura Wars: So Long, My Love; ".gro3", usually inside ".urs" archives; little endian). A GRO3 is a chunk container (G3MD, HMDL/KFMD ...); the KFMD mesh
// data is a PlayStation-2 style VIF packet stream: per strip a quadruple of UNPACK commands -- V4-32 (3 x f32 position + a flag word), V3-32 (normals as s32 / 32768), S-8 (one byte per
// vertex) and V2-16 (texcoords / 4096) -- all with the same vertex count. Every such quadruple is exported as one triangle strip of "<name>.gro3.d/<name>.obj" in model space (the
// bone/skin data are not applied). The ".hmdl" members of ".amf"/".urs" archives wrap the same KFMD stream and go through the same exporter.
static float gro3_f32 (const u8 *p)
{
	const uint v = rd_le32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static enumError extract_sega_gro3_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".gro3") && !is_ext (arg, ".hmdl"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x100 || (memcmp (c, "GRO3", 4) && memcmp (c, "HMDL", 4)))
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	uint nstrip = 0, ntri = 0, nv = 0;
	for (size_t p = 0x10; p + 4 <= cs; p += 4)
	{
		if (c[p + 3] != 0x6c || c[p + 2] < 3 || c[p + 2] > 80)
			continue;
		const uint n = c[p + 2];
		const size_t p68 = p + 4 + 16 * n;
		const size_t p62 = p68 + 4 + 12 * n;
		const size_t p65 = p62 + 4 + ((n + 3) & ~3u);
		const size_t end = p65 + 4 + 4 * n;
		if (end > cs || c[p68 + 3] != 0x68 || c[p68 + 2] != n || c[p62 + 3] != 0x62 || c[p62 + 2] != n || c[p65 + 3] != 0x65 || c[p65 + 2] != n)
			continue;
		bool good = true;
		for (uint i = 0; i < n && good; i++)
		{
			const float x = gro3_f32 (c + p + 4 + 16 * i), y = gro3_f32 (c + p + 8 + 16 * i), z = gro3_f32 (c + p + 12 + 16 * i);
			good = x > -1e5f && x < 1e5f && y > -1e5f && y < 1e5f && z > -1e5f && z < 1e5f;
		}
		if (!good)
			continue;
		sumo_printf (&b, "g strip%u\n", nstrip);
		for (uint i = 0; i < n; i++)
		{
			const u8 *v = c + p + 4 + 16 * i;
			sumo_printf (&b, "v %g %g %g\n", gro3_f32 (v), gro3_f32 (v + 4), gro3_f32 (v + 8));
		}
		for (uint i = 0; i < n; i++)
		{
			const u8 *v = c + p68 + 4 + 12 * i;
			sumo_printf (&b, "vn %g %g %g\n", (int)rd_le32 (v) / 32768.0, (int)rd_le32 (v + 4) / 32768.0, (int)rd_le32 (v + 8) / 32768.0);
		}
		for (uint i = 0; i < n; i++)
		{
			const u8 *v = c + p65 + 4 + 4 * i;
			sumo_printf (&b, "vt %g %g\n", (short)rd_le16 (v) / 4096.0, 1.0 - (short)rd_le16 (v + 2) / 4096.0);
		}
		for (uint i = 2; i < n; i++)
		{
			const uint a = nv + i - 1, bb = nv + i - 2, cc = nv + i + 1;
			const uint q[3] = { (i & 1) ? bb : a, (i & 1) ? a : bb, cc };
			sumo_printf (&b, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", q[0], q[0], q[0], q[1], q[1], q[1], q[2], q[2], q[2]);
			ntri++;
		}
		nv += n;
		nstrip++;
		p = end - 4;
	}
	// Models without normals or texcoords store plain V4-32 position triples (3 vertices per UNPACK, 52 bytes apart): one triangle each.
	if (!ntri && c)
		for (size_t p = 0x10; p + 52 <= cs; p += 4)
		{
			if (c[p + 3] != 0x6c || c[p + 2] != 3 || p + 56 > cs || c[p + 52 + 3] != 0x6c || c[p + 52 + 2] != 3)
				continue;
			bool good = true;
			for (uint i = 0; i < 9 && good; i++)
			{
				const float f = gro3_f32 (c + p + 4 + 16 * (i / 3) + 4 * (i % 3));
				good = f > -1e5f && f < 1e5f;
			}
			if (!good)
				continue;
			for (uint i = 0; i < 3; i++)
			{
				const u8 *v = c + p + 4 + 16 * i;
				sumo_printf (&b, "v %g %g %g\n", gro3_f32 (v), gro3_f32 (v + 4), gro3_f32 (v + 8));
			}
			sumo_printf (&b, "f %u %u %u\n", nv + 1, nv + 2, nv + 3);
			nv += 3;
			ntri++;
			nstrip++;
			p += 48;
		}
	FREE (c);
	enumError err = ERR_NOTHING_TO_DO;
	if (b.ok && b.n && ntri && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".obj");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SEGA-GRO3:%s (%u strips, %u triangles) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nstrip, ntri, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
