// Mercury Meltdown Revolution "*.paq" (Ignition Banbury): big-endian {u32 version 2001, u32 count, u32 data start (= 0x10 + count * 16), u32 0} then count * {u32 name hash, u32 size, u32 offset, u32 0}.
// There are no names; members go to "<file>.d/NNNN_<hash>.<ext>" (".arc" for U.8 archives, ".txt" for text, ".tspa"/".fnt1" for the "TSPA"/"FNT1" resources, else ".bin") and are recursed.
static enumError extract_mmpaq_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".paq"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *tab = 0, *buf = 0;
	u8 h[16];
	if (fread (h, 1, sizeof (h), f) != sizeof (h) || be32 (h) != 2001 || fseeko (f, 0, SEEK_END))
		goto done;
	{
		const u64 fsz = (u64)ftello (f);
		const uint n = be32 (h + 4);
		if (!n || n > 100000 || be32 (h + 8) != 0x10 + n * 16 || be32 (h + 8) > fsz)
			goto done;
		tab = MALLOC ((size_t)n * 16);
		if (fseeko (f, 16, SEEK_SET) || fread (tab, 1, (size_t)n * 16, f) != (size_t)n * 16)
			goto done;
		for (uint i = 0; i < n; i++)
			if ((u64)be32 (tab + i * 16 + 4) + be32 (tab + i * 16 + 8) > fsz || be32 (tab + i * 16 + 12))
				goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT PAQ:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		size_t cap = 0;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = tab + i * 16;
			const u32 hash = be32 (e), size = be32 (e + 4), off = be32 (e + 8);
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
			ccp ext = "bin";
			if (size >= 4 && !memcmp (buf, "U\xaa\x38\x2d", 4))
				ext = "arc";
			else if (size >= 4 && !memcmp (buf, "TSPA", 4))
				ext = "tspa";
			else if (size >= 4 && !memcmp (buf, "FNT1", 4))
				ext = "fnt1";
			else
			{
				bool text = size > 4;
				for (uint k = 0; k < 64 && k < size && text; k++)
					if (buf[k] > 0x7e || (buf[k] < 0x20 && buf[k] != '\r' && buf[k] != '\n' && buf[k] != '\t'))
						text = false;
				if (text)
					ext = "txt";
			}
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%04u_%08x.%s", dest, i, hash, ext);
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
