// Our House - Party! "*.wtpak": be32 crc, be32 1, be32 file count n, be32 ?, then n entries {be32 size, be32 offset, be32 name hash} from +0x10; the first member starts right after the table (aligned) and the members follow each other up to the end of the file. Members are streamed to "<file>.d/NNNN_<hash>.bin".
static enumError extract_wtpak_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".wtpak"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *tab = 0, *buf = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const off_t fsz = ftello (f);
		u8 head[16];
		if (fsz < 0x100 || fseeko (f, 0, SEEK_SET) || fread (head, 1, 16, f) != 16)
			goto done;
		const u32 n = be32 (head + 8);
		if (be32 (head + 4) != 1 || !n || n > 200000 || 16 + 12 * (off_t)n > fsz)
			goto done;
		tab = MALLOC (12 * (size_t)n);
		if (fread (tab, 1, 12 * (size_t)n, f) != 12 * (size_t)n)
			goto done;
		u64 next = be32 (tab + 4);
		if (next < 16 + 12 * (u64)n || next > 16 + 12 * (u64)n + 0x100)
			goto done;
		for (uint i = 0; i < n; i++)
		{
			const u32 s = be32 (tab + 12 * i), o = be32 (tab + 12 * i + 4);
			if (o != next)
				goto done;
			next = (u64)o + s;
		}
		if (next != (u64)fsz)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT WTPAK:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		const size_t BS = 1 << 20;
		buf = MALLOC (BS);
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u32 s = be32 (tab + 12 * i), o = be32 (tab + 12 * i + 4), h = be32 (tab + 12 * i + 8);
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%04u_%08x.bin", dest, i, h);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && s)
			{
				fseeko (f, o, SEEK_SET);
				for (u32 left = s; left && !err;)
				{
					const size_t k = left < BS ? left : BS;
					if (fread (buf, 1, k, f) != k || fwrite (buf, 1, k, F.f) != k)
						err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Copying %zu bytes failed: %s\n", k, path);
					left -= (u32)k;
				}
			}
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (tab);
	FREE (buf);
	fclose (f);
	return err;
}
