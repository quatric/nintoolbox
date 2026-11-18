// Crystal Dynamics "bigfile.000.wii-w" / "chapterN.000.wii-w" archives (Tomb Raider: Anniversary): big-endian {u32 count, count * u32 name hash (ascending), count * {u32 size, u32 aux, u32 locale mask, u32 0}}; file data follows from the next 0x800 boundary, stored back to back in table order with every entry padded to 0x800 (records with identical {size, aux} share the first copy) so that the last one ends exactly at the end of the archive. Names are only stored as hashes, so files are written to "<file>.d/NNNN_<hash>[_<locale>].<ext>" (".raw" for "!WAR" chunk files, ".bin" otherwise).
static enumError extract_cdbigfile_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".wii-w"))
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
		u8 hd[4];
		if (fsz < 0x1000 || fseeko (f, 0, SEEK_SET) || fread (hd, 1, 4, f) != 4)
			goto done;
		const uint n = be32 (hd);
		if (!n || n > 20000)
			goto done;
		const size_t tsz = 4 + 20 * (size_t)n;
		tab = MALLOC (tsz);
		if (fseeko (f, 0, SEEK_SET) || fread (tab, 1, tsz, f) != tsz)
			goto done;
		const u8 *rec = tab + 4 + 4 * (size_t)n;
		off_t o = (tsz + 0x7ff) & ~(size_t)0x7ff;
		off_t *offs = CALLOC (n, sizeof (off_t));
		for (uint i = 0; i < n; i++)
		{
			if (i && be32 (tab + 4 + 4 * i) < be32 (tab + 4 * i))
				goto done_offs;
			uint j;
			for (j = 0; j < i; j++)
				if (!memcmp (rec + 16 * j, rec + 16 * i, 8))
					break;
			if (j < i)
			{
				offs[i] = offs[j];
				continue;
			}
			offs[i] = o;
			o += ((off_t)be32 (rec + 16 * i) + 0x7ff) & ~(off_t)0x7ff;
		}
		if (o != fsz)
			goto done_offs;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT CDBIGFILE:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u32 s = be32 (rec + 16 * i), loc = be32 (rec + 16 * i + 8);
			u8 *buf = MALLOC (s ? s : 1);
			if (fseeko (f, offs[i], SEEK_SET) || fread (buf, 1, s, f) != s)
				err = ERR_READ_FAILED;
			else
			{
				char path[PATH_MAX];
				const int raw = s >= 4 && !memcmp (buf, "!WAR", 4);
				if (loc == 0xffffffff)
					snprintf (path, sizeof (path), "%s/%04u_%08x.%s", dest, i, be32 (tab + 4 + 4 * i), raw ? "raw" : "bin");
				else
					snprintf (path, sizeof (path), "%s/%04u_%08x_%08x.%s", dest, i, be32 (tab + 4 + 4 * i), loc, raw ? "raw" : "bin");
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (F.f && s && fwrite (buf, 1, s, F.f) != s)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
				ResetFile (&F, opt_preserve);
			}
			FREE (buf);
		}
	done_offs:
		FREE (offs);
	}
done:
	FREE (tab);
	fclose (f);
	return err;
}
