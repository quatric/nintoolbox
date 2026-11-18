// Sega "gos1" object-set packs (Ghost Squad "*.gos"): big-endian {u32 unknown, "gos1", u16 count, u16 0, count * u32 member offset}; the offset table ends exactly where the first member starts, members run back to back and the last one ends at the end of the file. Every member starts with its NUL-terminated name, which is used for "<file>.d/NNN_<name>.bin".
static enumError extract_gos1_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".gos"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *d = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const long sz = ftell (f);
		if (sz < 0x40 || sz > 0x20000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)sz);
		if (fread (d, 1, (size_t)sz, f) != (size_t)sz || memcmp (d + 4, "gos1", 4))
			goto done;
		const uint n = be16 (d + 8);
		if (!n || be16 (d + 10) || n > 10000 || 12 + 4 * (size_t)n > (size_t)sz || be32 (d + 12) != 12 + 4 * n)
			goto done;
		for (uint i = 0; i < n; i++)
		{
			const u32 o = be32 (d + 12 + 4 * i), e = i + 1 < n ? be32 (d + 16 + 4 * i) : (u32)sz;
			if (e <= o || e > (u32)sz)
				goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT GOS1:%s (%u members) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u32 o = be32 (d + 12 + 4 * i), e = i + 1 < n ? be32 (d + 16 + 4 * i) : (u32)sz;
			char nm[40];
			uint k = 0;
			while (k < 32 && o + k < e && d[o + k] >= 0x21 && d[o + k] < 0x7f && d[o + k] != '/' && d[o + k] != '\\')
			{
				nm[k] = d[o + k];
				k++;
			}
			nm[k] = 0;
			char path[PATH_MAX];
			if (k >= 3 && (o + k >= e || !d[o + k]))
				snprintf (path, sizeof (path), "%s/%03u_%s.bin", dest, i, nm);
			else
				snprintf (path, sizeof (path), "%s/%03u.bin", dest, i);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (d + o, 1, e - o, F.f) != e - o)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e - o, path);
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (d);
	fclose (f);
	return err;
}
