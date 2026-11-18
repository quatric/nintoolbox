// "*.brtex" texture packs (TV Show King Party, We Sing, Showtime Championship Boxing): the file is a 6-byte prefix "\xffLZMA\0" followed by a standard LZMA-alone stream (props, dictionary size, 8-byte size). The decompressed data (a BRRES) is written to "<file>.brres" and handed to the normal extractors.
static enumError extract_lzmabrtex_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".brtex"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *d = 0, *out = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const long sz = ftell (f);
		if (sz < 32 || sz > 0x20000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)sz);
		if (fread (d, 1, (size_t)sz, f) != (size_t)sz || memcmp (d, "\xff" "LZMA\0", 6))
			goto done;
		uint outsz = 0;
		if (DecodeLZMAbin (&out, &outsz, 0, d + 6, (uint)sz - 6) || !out || !outsz)
			goto done;
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT LZMABRTEX:%s (%u bytes)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, outsz);
		err = ERR_OK;
		if (!testmode)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s.brres", arg);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (out, 1, outsz, F.f) != outsz)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", outsz, path);
			ResetFile (&F, opt_preserve);
			if (!err)
			{
				const enumError sub = extract_one_file (path, 0, depth + 1);
				if (sub != ERR_NOTHING_TO_DO && err < sub)
					err = sub;
			}
		}
	}
done:
	FREE (d);
	FREE (out);
	fclose (f);
	return err;
}
