// Midway sprite sets (".spr", members of TNA Impact! ".WAD" packs; big endian): u32 size @0x0c, name count @0x10, name table offset @0x14 (20-byte names), sprite count @0x18,
// sprite offsets from 0x1c. A sprite record is 36 bytes: a pad word then four {u, v} float corners (normalised coordinates into the named ".tim" atlas, order (u0,v0)
// (u1,v0) (u1,v1) (u0,v1)). Any geometry trailing the last record is ignored. Written as "<name>.txt" with "index atlas u0 v0 u1 v1" lines.
static enumError extract_midway_spr_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".spr"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const size_t nn = sz > 0x20 ? rd_be32 (raw + 0x10) : 0, noff = sz > 0x20 ? rd_be32 (raw + 0x14) : 0, ns = sz > 0x20 ? rd_be32 (raw + 0x18) : 0;
	if (nn == 1 && ns && ns < 0x10000 && noff >= 0x1c + 4 * ns && noff + 20 <= sz && rd_be32 (raw + 0x0c) <= sz)
	{
		char *txt = CALLOC (ns * 128 + 256, 1);
		bool ok = txt != 0;
		size_t o = 0;
		if (ok)
			o = (size_t)sprintf (txt, "# atlas %.16s\n# index u0 v0 u1 v1 (corner order u0v0 u1v0 u1v1 u0v1)\n", (ccp)raw + noff);
		for (size_t i = 0; ok && i < ns; i++)
		{
			const size_t r = rd_be32 (raw + 0x1c + 4 * i);
			if (r + 36 > sz)
			{
				ok = false;
				break;
			}
			float f[8];
			for (uint k = 0; k < 8; k++)
			{
				const uint v = rd_be32 (raw + r + 4 + 4 * k);
				memcpy (f + k, &v, 4);
			}
			o += (size_t)sprintf (txt + o, "%zu %.5f %.5f %.5f %.5f\n", i, f[0], f[1], f[2], f[5]);
		}
		if (ok)
		{
			char dest[PATH_MAX], name[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (name, '.');
			if (dot) strcpy (dot, ".txt");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT MIDWAY-SPR:%s (%zu sprites) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ns, dest, name);
			err = shade_write (arg, dest, basedir, name, (u8 *)txt, (uint)o);
		}
		FREE (txt);
	}
	FREE (raw);
	return err;
}
