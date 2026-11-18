// "GEEK" archives ("*.csa", Barbie Horse Adventures - Riding Camp "data.csa", 755 MB): little-endian {"GEEK", u32 0x100, u32 data start, u32 flags, u32 file count} then count * 136-byte entries {u32 offset, u32 size, char name[128]} (backslash paths).
// Files are streamed to "<file>.d/<path>" and recursed.
static enumError extract_geekcsa_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".csa"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *tab = 0, *buf = 0;
	u8 hdr[0x14];
	if (fread (hdr, 1, sizeof (hdr), f) != sizeof (hdr) || memcmp (hdr, "GEEK", 4) || fseeko (f, 0, SEEK_END))
		goto done;
	{
		const u64 fsz = (u64)ftello (f);
		const uint n = rd_le32 (hdr + 16);
		if (!n || n > 1000000 || 0x14 + (u64)n * 136 > fsz)
			goto done;
		tab = MALLOC ((size_t)n * 136);
		if (fseeko (f, 0x14, SEEK_SET) || fread (tab, 1, (size_t)n * 136, f) != (size_t)n * 136)
			goto done;
		for (uint i = 0; i < n; i++)
			if ((u64)rd_le32 (tab + i * 136) + rd_le32 (tab + i * 136 + 4) > fsz || !tab[i * 136 + 8] || tab[i * 136 + 135])
				goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT GEEK:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		size_t cap = 0;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = tab + (size_t)i * 136;
			const u32 off = rd_le32 (e), size = rd_le32 (e + 4);
			char name[130];
			uint k = 0;
			for (; k < 128 && e[8 + k]; k++)
				name[k] = e[8 + k] == '\\' ? '/' : e[8 + k];
			name[k] = 0;
			if (name[0] == '/' || strstr (name, "..") || !size)
				continue;
			if (size > cap)
			{
				FREE (buf);
				cap = size;
				buf = MALLOC (cap);
			}
			if (fseeko (f, (off_t)off, SEEK_SET) || fread (buf, 1, size, f) != size)
				continue;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s", dest, name);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (buf, 1, size, F.f) != size)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
			ResetFile (&F, opt_preserve);
		}
		if (!err && !testmode)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (tab);
	FREE (buf);
	fclose (f);
	return err;
}
