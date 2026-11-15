// "NLCM" file index (Rune Factory Frontier, Wii: RUNEFACTORY.bin + RUNEFACTORY.dat): BE
//   {"NLCM", u32 header size (0x38), u32 name length (0x14), u32 entry count, u32 data file count,
//    data file name (NUL padded), ...}, then `count` 16 byte entries {u32 size, u32 0, u32 offset, u32 0}
// (a pair of 64 bit values stored as low/high words); entries sit at 0x800-aligned offsets of the .dat file.
// The files carry no names: they are carved by streaming from the sibling data file into "NNNNN.bin" and then
// extracted recursively.
static enumError extract_nlcm_file (ccp arg, ccp basedir, uint depth)
{
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 hd[0x38];
	if (fread (hd, 1, sizeof (hd), f) != sizeof (hd) || memcmp (hd, "NLCM", 4) || rd_be32 (hd + 4) != 0x38
	    || rd_be32 (hd + 16) != 1)
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	const u32 count = rd_be32 (hd + 12);
	if (!count || count > 1000000 || fseeko (f, 0, SEEK_END) || (u64)ftello (f) != 0x38 + (u64)count * 16)
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	char datname[0x25];
	memcpy (datname, hd + 0x14, 0x24);
	datname[0x24] = 0;
	if (!datname[0] || strchr (datname, '/') || strchr (datname, '\\'))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	u8 *tab = MALLOC ((size_t)count * 16);
	if (!tab || fseeko (f, 0x38, SEEK_SET) || fread (tab, 1, (size_t)count * 16, f) != (size_t)count * 16)
	{
		FREE (tab);
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	fclose (f);
	char dpath[PATH_MAX];
	snprintf (dpath, sizeof (dpath), "%s", arg);
	char *sl = strrchr (dpath, '/');
	if (sl)
		snprintf (sl + 1, sizeof (dpath) - (size_t)(sl + 1 - dpath), "%s", datname);
	else
		snprintf (dpath, sizeof (dpath), "%s", datname);
	FILE *df = fopen (dpath, "rb");
	if (!df || fseeko (df, 0, SEEK_END))
	{
		if (df)
			fclose (df);
		FREE (tab);
		return ERR_NOTHING_TO_DO;
	}
	const u64 dsz = (u64)ftello (df);
	for (u32 i = 0; i < count; i++)
	{
		const u8 *e = tab + 16 * (size_t)i;
		if (rd_be32 (e + 4) || rd_be32 (e + 12) || (u64)rd_be32 (e) + rd_be32 (e + 8) > dsz)
		{
			fclose (df);
			FREE (tab);
			return ERR_NOTHING_TO_DO;
		}
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NLCM:%s (%u entries from %s) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, datname, dest);
	enumError err = ERR_OK;
	u8 *buf = MALLOC (1 << 20);
	for (u32 i = 0; !err && !testmode && i < count && buf; i++)
	{
		const u8 *e = tab + 16 * (size_t)i;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%05u.bin", dest, basedir ? basedir : "", i);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && !fseeko (df, (off_t)rd_be32 (e + 8), SEEK_SET))
			for (u64 left = rd_be32 (e); left && !err;)
			{
				const size_t k = left < (1 << 20) ? (size_t)left : (1 << 20);
				if (fread (buf, 1, k, df) != k || fwrite (buf, 1, k, F.f) != k)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Copying member failed: %s\n", path);
				left -= k;
			}
		ResetFile (&F, opt_preserve);
	}
	FREE (buf);
	FREE (tab);
	fclose (df);
	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}
