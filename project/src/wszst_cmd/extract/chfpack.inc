// Disney Epic Mickey 2 "*.chf" packs (animations.chf): big-endian {u32 magic 0x85620240, u32 count, 0x18 bytes 0}, then count 12-byte entries {u32 id/hash, u32 offset, u32 size} and the data (0x10 aligned, back to back; the last entry ends exactly at the end of the file). Files are written to "<file>.d/NNNN_<id>.bin".
static enumError extract_chfpack_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".chf"))
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
		u8 hd[8];
		if (fsz < 0x40 || fseeko (f, 0, SEEK_SET) || fread (hd, 1, 8, f) != 8 || be32 (hd) != 0x85620240)
			goto done;
		const uint n = be32 (hd + 4);
		if (!n || n > 1000000 || 0x20 + 12 * (size_t)n > (size_t)fsz)
			goto done;
		const size_t tsz = 0x20 + 12 * (size_t)n;
		tab = MALLOC (tsz);
		if (fseeko (f, 0, SEEK_SET) || fread (tab, 1, tsz, f) != tsz)
			goto done;
		const u8 *e = tab + 0x20;
		for (uint i = 0; i < n; i++)
		{
			const u32 o = be32 (e + 12 * i + 4), s = be32 (e + 12 * i + 8);
			if (o < tsz || (u64)o + s > (u64)fsz || (i && o < be32 (e + 12 * i - 8) + be32 (e + 12 * i - 4)))
				goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT CHFPACK:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u32 o = be32 (e + 12 * i + 4), s = be32 (e + 12 * i + 8);
			u8 *buf = MALLOC (s ? s : 1);
			if (fseeko (f, o, SEEK_SET) || fread (buf, 1, s, f) != s)
				err = ERR_READ_FAILED;
			else
			{
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%04u_%08x.bin", dest, i, be32 (e + 12 * i));
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (F.f && s && fwrite (buf, 1, s, F.f) != s)
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
