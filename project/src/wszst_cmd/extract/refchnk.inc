// Reflections "CHNK" containers (Driver: Parallel Lines "*.d4c" level files, and the chunks nested in them): little-endian {"CHNK", u32 total size, u32 entry count, u32 3} then count * 16-byte entries {char tag[4], u32 offset, u32 meta, u32 size}.
// Offsets are relative to the start of the container. Members go to "<file>.d/NNNN_<tag>.<chnk|bin>" (".chnk" when the member is itself a CHNK container) and are recursed.
static enumError extract_refchnk_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".d4c") && !is_ext (arg, ".chnk"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *tab = 0, *buf = 0;
	u8 hdr[16];
	if (fread (hdr, 1, sizeof (hdr), f) != sizeof (hdr) || memcmp (hdr, "CHNK", 4) || fseeko (f, 0, SEEK_END))
		goto done;
	{
		const u64 fsz = (u64)ftello (f);
		const uint n = rd_le32 (hdr + 8);
		if (!n || n > 100000 || 16 + (u64)n * 16 > fsz || rd_le32 (hdr + 4) > fsz)
			goto done;
		tab = MALLOC ((size_t)n * 16);
		if (fseeko (f, 16, SEEK_SET) || fread (tab, 1, (size_t)n * 16, f) != (size_t)n * 16)
			goto done;
		for (uint i = 0; i < n; i++)
		{
			const u8 *e = tab + i * 16;
			for (int k = 0; k < 4; k++)
				if (e[k] < 'A' || e[k] > 'Z')
					goto done;
			if ((u64)rd_le32 (e + 4) + rd_le32 (e + 12) > fsz)
				goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT CHNK:%s (%u chunks) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		size_t cap = 0;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = tab + i * 16;
			const u32 off = rd_le32 (e + 4), size = rd_le32 (e + 12);
			if (!size)
				continue;
			if (size > cap)
			{
				FREE (buf);
				cap = size;
				buf = MALLOC (cap);
			}
			if (fseeko (f, off, SEEK_SET) || fread (buf, 1, size, f) != size)
				continue;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%04u_%.4s.%s", dest, i, (ccp)e, size >= 16 && !memcmp (buf, "CHNK", 4) ? "chnk" : "bin");
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
