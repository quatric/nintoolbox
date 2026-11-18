// Ford Racing - Off Road "graphics.wig": le32 inflated size and a zlib stream. The inflated data is le32 count, 12 zero bytes, count 16-byte entries {le32 offset, char name[12]} and the images at 0x10 + 16 * count + offset (each with its own 0x40 byte header: le32 0, 1, width, height, ...). Images are written to "<file>.d/<name>.img".
static enumError extract_fordwig_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".wig"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *raw = 0, *d = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const long sz = ftell (f);
		if (sz < 0x20 || sz > 0x20000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		raw = MALLOC ((size_t)sz);
		if (fread (raw, 1, (size_t)sz, f) != (size_t)sz)
			goto done;
		const u32 dl = rd_le32 (raw);
		if (dl < 0x20 || dl > 0x20000000 || raw[4] != 0x78)
			goto done;
		d = MALLOC (dl);
		uLongf dlen = dl;
		if (uncompress (d, &dlen, raw + 4, (uLong)(sz - 4)) != Z_OK || dlen != dl)
			goto done;
		const u32 cnt = rd_le32 (d);
		if (!cnt || cnt > 1000 || 0x10 + 16 * (size_t)cnt > dl)
			goto done;
		const size_t base = 0x10 + 16 * (size_t)cnt;
		u32 prev = 0;
		for (uint i = 0; i < cnt; i++)
		{
			const u8 *e = d + 0x10 + 16 * i;
			const u32 o = rd_le32 (e);
			if ((i && o < prev) || base + o > dl || e[4] == 0 || e[15] != 0)
				goto done;
			prev = o;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT FORDWIG:%s (%u images) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, cnt, dest);
		err = ERR_OK;
		for (uint i = 0; i < cnt && !err && !testmode; i++)
		{
			const u8 *e = d + 0x10 + 16 * i;
			const size_t o = base + rd_le32 (e);
			const size_t end = i + 1 < cnt ? base + rd_le32 (e + 16) : dl;
			char nm[13], path[PATH_MAX];
			memcpy (nm, e + 4, 12);
			nm[12] = 0;
			for (char *c = nm; *c; c++)
				if (*c == '/' || *c == '\\')
					*c = '_';
			snprintf (path, sizeof (path), "%s/%s.img", dest, nm);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && end > o && fwrite (d + o, 1, end - o, F.f) != end - o)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", end - o, path);
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (raw);
	FREE (d);
	fclose (f);
	return err;
}
