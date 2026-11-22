// Voxler "*.mesh.bin" meshes (Let's Sing, The Voice: UI quads such as fond / mask / light / particule), little endian:
//   {u8 4, "MESH"}, {u8 4, "VTEX", u32 count, count * 3 floats}, then optionally {u8 4, "UV00", u32 count, count * 2 floats},
//   ending exactly at the end of the file. There is no index list: the vertices are consecutive triangles.
// Written as Wavefront OBJ "<file>.obj" ("vt" when the UV count matches, faces only when the vertex count is a multiple of 3).
static float meshbin_f (const u8 *p)
{
	const u32 v = rd_le32 (p);
	float f;
	memcpy (&f, &v, sizeof (f));
	return f;
}

static enumError extract_meshbin_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t cx;
	memset (&cx, 0, sizeof (cx));
	cx.ok = true;
	if (sz < 19 || memcmp (raw, "\x04MESH\x04VTEX", 10))
		goto done;
	{
		const u64 nv = rd_le32 (raw + 10), vend = 14 + nv * 12;
		u64 nt = 0, end = vend;
		if (!nv || vend > sz)
			goto done;
		if (end < sz)
		{
			if (end + 9 > sz || memcmp (raw + end, "\x04UV00", 5))
				goto done;
			nt = rd_le32 (raw + end + 5);
			end += 9 + nt * 8;
		}
		if (end != sz)
			goto done;
		for (u64 i = 0; i < nv; i++)
			pcr_emit (&cx, "v %.9g %.9g %.9g\n", meshbin_f (raw + 14 + i * 12), meshbin_f (raw + 18 + i * 12), meshbin_f (raw + 22 + i * 12));
		const bool uv = nt == nv;
		for (u64 i = 0; uv && i < nt; i++)
			pcr_emit (&cx, "vt %.9g %.9g\n", meshbin_f (raw + vend + 9 + i * 8), meshbin_f (raw + vend + 13 + i * 8));
		for (u64 i = 0; i + 2 < nv && nv % 3 == 0; i += 3)
			if (uv)
				pcr_emit (&cx, "f %u/%u %u/%u %u/%u\n", (uint)i + 1, (uint)i + 1, (uint)i + 2, (uint)i + 2, (uint)i + 3, (uint)i + 3);
			else
				pcr_emit (&cx, "f %u %u %u\n", (uint)i + 1, (uint)i + 2, (uint)i + 3);
	}
	if (cx.ok && cx.len)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.obj", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT MESHBIN:%s -> OBJ:%s\n", arg, dest);
			err = ERR_OK;
		}
		else
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f && fwrite (cx.out, 1, cx.len, F.f) != cx.len)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)cx.len, dest);
			ResetFile (&F, opt_preserve);
			if (verbose >= 0 && !err)
				fprintf (stdlog, "%sEXTRACT MESHBIN:%s -> OBJ:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
done:
	FREE (cx.out);
	FREE (raw);
	return err;
}
