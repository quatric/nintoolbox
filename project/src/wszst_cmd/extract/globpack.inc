// Disney Wreck-It Ralph "*.glob": be32 "GLOB", be32 entry count + 1 (entries incl. terminator), be32 ?, be32 data start, be32 ?, then count-1 12-byte entries (8 footer bytes follow, data start = 0x1c + 12*entries) {u32 id, u32 offset, u32 size} from 0x14; the last entry has id 0x7fffffff and size 0 and ends at EOF. Members are written as "<file>.d/NNNN_<id>.bin".
static enumError extract_globpack_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".glob"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *d = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const long sz = ftell (f);
		if (sz < 0x100 || sz > 0x40000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)sz);
		if (fread (d, 1, (size_t)sz, f) != (size_t)sz)
			goto done;
		if (memcmp (d, "GLOB", 4))
			goto done;
		const u32 cnt = be32 (d + 4) - 1, start = be32 (d + 12);
		if (cnt < 2 || cnt > 100000 || start != 0x1c + 12 * cnt || start > (size_t)sz)
			goto done;
		u64 next = start;
		for (uint i = 0; i < cnt; i++)
		{
			const u8 *e = d + 0x14 + 12 * i;
			const u32 id = be32 (e), o = be32 (e + 4), s = be32 (e + 8);
			if (o < next || (u64)o + s > (u64)sz)
				goto done;
			if (i == cnt - 1 && (id != 0x7fffffff || s || o != (u32)sz))
				goto done;
			next = (u64)o + s;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT GLOB:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, cnt - 1, dest);
		err = ERR_OK;
		for (uint i = 0; i + 1 < cnt && !err && !testmode; i++)
		{
			const u8 *e = d + 0x14 + 12 * i;
			const u32 id = be32 (e), o = be32 (e + 4), s = be32 (e + 8);
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%04u_%08x.bin", dest, i, id);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && s && fwrite (d + o, 1, s, F.f) != s)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (d);
	fclose (f);
	return err;
}
