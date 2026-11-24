// Eurocom EngineX "Filelist" packs (GoldenEye 007, Wii: "Filelist.bin" + "Filelist.000", big endian): the index "Filelist.bin" starts {u32 7, u32 index size, u32 entry count + 1, u32 0x10000, u32 offset of the name
// table - 0x10, 5, ...}; from 0x28 one 28 byte record per file {u32 byte offset into the ".000" data file (0x800 aligned), u32 0, u32 size, u32 type word, u32 flags, u32 flags2, u32 1}. At the name table
// offset + 0x10 follow entry count + 1 self-relative u32 pointers and as many NUL-terminated strings, string i the path of file i ("x:\\bondx\\binary\\bondx.evn", "x:\\bondx\\binary\\_bin_wii\\as_bondbreach.edb",
// ...; the retail index has one string more than records, homebtnicon.tpl). Byte j of string i is stored as plain + j + 0xea + i (mod 256) (the same start key in Filelist.bin and the File_USA/FRE/SPA
// indexes). Opening the index writes every file of the data file under its path after "\\_bin_wii\\" or else its base name (as_bondbreach.edb, homebutton2/speakerse.arc, bondx.evn), or as
// "<index>.d/NNNN.bin" when the names do not decode, and extracts them recursively. 918 files in the retail Filelist; every name's extension matches its data (GEOM .edb, U8 .arc, UTF-16 .csv).

// Decode the names of the n files (n + 1 strings); returns a malloc'd array of paths (one buffer) or NULL when a name is missing or unsafe.
static char **eurocom_filelist_names (const u8 *raw, size_t sz, u32 n)
{
	u64 p = rd_be32 (raw + 0x10) + 0x10ull + 4ull * (n + 1);
	if (p >= sz)
		return 0;
	char **names = MALLOC ((n + 1) * sizeof (char *) + (sz - p) + 1);
	char *out = (char *)(names + n + 1);
	for (u32 i = 0; i <= n; i++)
	{
		const u8 key = (u8)(0xea + i);
		char *start = out;
		for (u32 j = 0;; j++, p++)
		{
			if (p >= sz)
				goto fail;
			const u8 c = (u8)(raw[p] - j - key);
			if (!c)
				break;
			if (c < 0x20 || c >= 0x7f)
				goto fail;
			*out++ = c == '\\' ? '/' : (char)c;
		}
		*out++ = 0;
		p++;
		char *rel = strstr (start, "/_bin_wii/");
		rel = rel ? rel + 10 : strrchr (start, '/') ? strrchr (start, '/') + 1 : start;
		if (i < n && (!*rel || strchr (rel, ':') || strstr (rel, "..")))
			goto fail;
		names[i] = rel;
	}
	for (u32 i = 0; i < n; i++)
		for (u32 k = 0; k < i; k++)
			if (!strcasecmp (names[i], names[k])) // case-insensitive file systems
				goto fail;
	return names;
fail:
	FREE (names);
	return 0;
}

static enumError extract_eurocom_filelist_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char data_path[PATH_MAX];
	snprintf (data_path, sizeof (data_path), "%s", arg);
	char *dot = strrchr (data_path, '.');
	struct stat st;
	const u32 n = sz >= 0x40 ? rd_be32 (raw + 8) - 1 : 0;
	bool ok = sz >= 0x40 && dot && rd_be32 (raw) == 7 && rd_be32 (raw + 4) == sz && rd_be32 (raw + 0xc) == 0x10000 && n && n < 0x100000 && 0x28 + 28ull * n <= sz;
	if (ok)
	{
		strcpy (dot, ".000");
		ok = !stat (data_path, &st) && st.st_size > 0;
	}
	u64 prev = 0;
	for (u32 i = 0; ok && i < n; i++)
	{
		const u8 *r = raw + 0x28 + 28 * (size_t)i;
		const u64 off = rd_be32 (r), size = rd_be32 (r + 8);
		ok = !rd_be32 (r + 4) && !(off & 0x7ff) && off >= prev && off + size <= (u64)st.st_size && size < 0x40000000;
		prev = off;
	}
	if (!ok)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT EUROCOM-FILELIST:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
	enumError err = ERR_OK;
	char **names = eurocom_filelist_names (raw, sz, n);
	FILE *df = testmode ? 0 : fopen (data_path, "rb");
	if (!testmode && !df)
		err = ERR_CANT_OPEN;
	for (u32 i = 0; !err && !testmode && i < n; i++)
	{
		const u8 *r = raw + 0x28 + 28 * (size_t)i;
		const size_t off = rd_be32 (r), size = rd_be32 (r + 8);
		u8 *buf = MALLOC (size ? size : 1);
		if (!buf || fseeko (df, (off_t)off, SEEK_SET) || (size && fread (buf, 1, size, df) != size))
		{
			FREE (buf);
			err = ERR_READ_FAILED;
			break;
		}
		char path[PATH_MAX];
		if (names)
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", names[i]);
		else
			snprintf (path, sizeof (path), "%s/%s%04u.bin", dest, basedir ? basedir : "", i);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && size && fwrite (buf, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", size, path);
		ResetFile (&F, opt_preserve);
		FREE (buf);
	}
	if (df)
		fclose (df);
	FREE (names);
	FREE (raw);
	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}
