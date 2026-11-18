// Arc Rise Fantasia "*.vld" packs (Btl/btl_*.vld): a sequence of "RTDP" archives, each padded to 0x800. Big-endian archive header {"RTDP", u32 header size (0x100, = start of the data), u32 count, u32 total size (header + data), ...}, count 0x28-byte entries from 0x20 {char name[0x20], u32 size, u32 offset relative to the data start}. The archives must tile the whole file. Members are written to "<file>.d/<NNN>_<first member stem>/<name>".
static enumError extract_rtdpvld_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".vld"))
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
		if (sz < 0x100 || sz > 0x40000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)sz);
		if (fread (d, 1, (size_t)sz, f) != (size_t)sz)
			goto done;
		size_t pos = 0;
		uint archives = 0;
		while (pos < (size_t)sz)
		{
			if (pos + 0x100 > (size_t)sz || memcmp (d + pos, "RTDP", 4))
				goto done;
			const u32 hs = be32 (d + pos + 4), cnt = be32 (d + pos + 8), tot = be32 (d + pos + 12);
			if (!cnt || cnt > 1000 || hs < 0x20 + 0x28 * cnt || hs > 0x1000 || tot < hs || pos + tot > (size_t)sz)
				goto done;
			u64 end = 0;
			for (uint i = 0; i < cnt; i++)
			{
				const u8 *e = d + pos + 0x20 + 0x28 * i;
				const u64 e1 = (u64)be32 (e + 0x20) + be32 (e + 0x24);
				if (e[0] && e1 > end)
					end = e1;
			}
			if (end + hs > tot)
				goto done;
			pos = (pos + tot + 0x7ff) & ~(size_t)0x7ff;
			archives++;
		}
		if (pos != (size_t)sz)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT RTDPVLD:%s (%u archives) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, archives, dest);
		err = ERR_OK;
		pos = 0;
		uint idx = 0;
		while (pos < (size_t)sz && !err && !testmode)
		{
			const u32 hs = be32 (d + pos + 4), cnt = be32 (d + pos + 8), tot = be32 (d + pos + 12);
			char stem[0x21];
			memcpy (stem, d + pos + 0x20, 0x20);
			stem[0x20] = 0;
			char *dot = strchr (stem, '.');
			if (dot)
				*dot = 0;
			for (char *c = stem; *c; c++)
				if (*c == '/' || *c == '\\')
					*c = '_';
			for (uint i = 0; i < cnt && !err; i++)
			{
				const u8 *e = d + pos + 0x20 + 0x28 * i;
				if (!e[0])
					continue;
				char nm[0x21];
				memcpy (nm, e, 0x20);
				nm[0x20] = 0;
				if (strchr (nm, '/') || strchr (nm, '\\') || !strcmp (nm, ".") || !strcmp (nm, ".."))
					continue;
				const u32 s = be32 (e + 0x20), o = be32 (e + 0x24);
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%03u_%s/%s", dest, idx, stem, nm);
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (F.f && s && fwrite (d + pos + hs + o, 1, s, F.f) != s)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
				ResetFile (&F, opt_preserve);
			}
			pos = (pos + tot + 0x7ff) & ~(size_t)0x7ff;
			idx++;
		}
	}
done:
	FREE (d);
	fclose (f);
	return err;
}
