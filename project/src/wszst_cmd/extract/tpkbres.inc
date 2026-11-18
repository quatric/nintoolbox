// Kidz Bop Dance Party "*.tpk" texture packs: be32 0, be32 0x10 (table offset), then ascending be32 offsets (zero terminated) of "bres" (BRRES) files; each BRRES length is at +8. Members are written to "<file>.d/NNNN.brres" and recursed.
static enumError extract_tpkbres_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".tpk"))
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
		if (be32 (d) || be32 (d + 4) != 0x10)
			goto done;
		uint cnt = 0;
		while (0x10 + 4 * (cnt + 1) <= (size_t)sz && be32 (d + 0x10 + 4 * cnt))
			cnt++;
		if (!cnt || cnt > 4096)
			goto done;
		u32 off[4096], len[4096];
		for (uint i = 0; i < cnt; i++)
		{
			off[i] = be32 (d + 0x10 + 4 * i);
			if (off[i] < 0x10 + 4 * cnt || (size_t)off[i] + 12 > (size_t)sz || memcmp (d + off[i], "bres", 4))
				goto done;
			len[i] = be32 (d + off[i] + 8);
			if (!len[i] || (size_t)off[i] + len[i] > (size_t)sz)
				goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT TPKBRES:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, cnt, dest);
		err = ERR_OK;
		for (uint i = 0; i < cnt && !err && !testmode; i++)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%04u.brres", dest, i);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (d + off[i], 1, len[i], F.f) != len[i])
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", len[i], path);
			ResetFile (&F, opt_preserve);
			if (!err)
				extract_one_file (path, 0, depth + 1);
		}
	}
done:
	FREE (d);
	fclose (f);
	return err;
}
