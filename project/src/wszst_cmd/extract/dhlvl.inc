// G.I. Joe: The Rise of Cobra "*.lvl_NNN" level packs: big-endian header of 0x80 bytes (u32 data start, u32 entry count, ...), then count 16-byte entries {u32 name hash, u32 offset, u32 size, u32 type}. Entries with size 0 are placeholders. Files are written to "<file>.d/NNNN_<hash>_t<type>.bin".
static enumError extract_dhlvl_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	ccp ext = strrchr (arg, '.');
	if (!ext || strncasecmp (ext, ".lvl_", 5) || strchr (ext, '/'))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *tab = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const off_t fsz = ftello (f);
		u8 hd[0x90];
		if (fsz < 0x100 || fseeko (f, 0, SEEK_SET) || fread (hd, 1, sizeof (hd), f) != sizeof (hd))
			goto done;
		const u32 data = be32 (hd);
		if (data < 0x90 || data > 0x100000 || data > fsz || (data & 0xf) || be32 (hd + 0x84) != data)
			goto done;
		const uint n = be32 (hd + 4);
		if (!n || 0x80 + 16 * (size_t)n > data)
			goto done;
		tab = MALLOC (data);
		if (fseeko (f, 0, SEEK_SET) || fread (tab, 1, data, f) != data)
			goto done;
		const u8 *e = tab + 0x80;
		for (uint i = 0; i < n; i++)
			if ((u64)be32 (e + 16 * i + 4) + be32 (e + 16 * i + 8) > (u64)fsz || be32 (e + 16 * i + 4) < data)
				goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT DHLVL:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u32 off = be32 (e + 16 * i + 4), s = be32 (e + 16 * i + 8);
			if (!s)
				continue;
			u8 *buf = MALLOC (s);
			if (fseeko (f, off, SEEK_SET) || fread (buf, 1, s, f) != s)
				err = ERR_READ_FAILED;
			else
			{
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%04u_%08x_t%u.bin", dest, i, be32 (e + 16 * i), be32 (e + 16 * i + 12));
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (F.f && fwrite (buf, 1, s, F.f) != s)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
				ResetFile (&F, opt_preserve);
			}
			FREE (buf);
		}
	}
done:
	FREE (tab);
	fclose (f);
	return err;
}
