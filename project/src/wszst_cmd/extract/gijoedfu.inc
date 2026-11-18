// G.I. Joe - The Rise of Cobra "GamePak*.dfu": be32 entry count, 5 more header words (24 bytes in all), then count 16-byte entries {u32 aux, u32 name hash, u32 offset, u32 size}. Data offsets are 0x800 aligned and inside the file. Members (streamed, the pack is ~1 GB) are written to "<file>.d/NNNN_<hash>.bin".
static enumError extract_gijoedfu_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".dfu"))
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
		u8 head[24];
		if (fsz < 0x1000 || fseeko (f, 0, SEEK_SET) || fread (head, 1, 24, f) != 24)
			goto done;
		const u32 cnt = be32 (head);
		if (!cnt || cnt > 100000 || be32 (head + 20) != 0x20 || 24 + 16 * (off_t)cnt > fsz)
			goto done;
		tab = MALLOC (16 * (size_t)cnt);
		if (fread (tab, 1, 16 * (size_t)cnt, f) != 16 * (size_t)cnt)
			goto done;
		for (uint i = 0; i < cnt; i++)
		{
			const u32 o = be32 (tab + 16 * i + 8), s = be32 (tab + 16 * i + 12);
			if ((o & 0x7ff) || (off_t)o + s > fsz || (o && o < 24 + 16 * cnt))
				goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT GIJOEDFU:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, cnt, dest);
		err = ERR_OK;
		const size_t BS = 1 << 20;
		buf = MALLOC (BS);
		for (uint i = 0; i < cnt && !err && !testmode; i++)
		{
			const u32 h = be32 (tab + 16 * i + 4), o = be32 (tab + 16 * i + 8), s = be32 (tab + 16 * i + 12);
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%04u_%08x.bin", dest, i, h);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && s)
			{
				fseeko (f, o, SEEK_SET);
				for (u32 left = s; left && !err;)
				{
					const size_t n = left < BS ? left : BS;
					if (fread (buf, 1, n, f) != n || fwrite (buf, 1, n, F.f) != n)
						err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Copying %zu bytes failed: %s\n", n, path);
					left -= (u32)n;
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
