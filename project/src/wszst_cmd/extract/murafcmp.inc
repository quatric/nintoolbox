// Muramasa - The Demon Blade ".abf" (MLIB resource name lists), ".esb" (EMBP typed record tables), ".mbs" and
// ".nsb" files are "FCMP" LZSS containers (see lib-muramasa.c). In decode mode the decompressed body is written to
// "<file>.dec" and, for the string-bearing MLIB / EMBP bodies, every NUL-terminated string of at least three
// characters (ASCII or Shift-JIS) to "<file>.strings.txt" as "offset<TAB>text" lines. The record layouts of the
// inner formats are not interpreted.
static enumError extract_murafcmp_file (ccp arg)
{
	if ((export_count <= 0 && !opt_decode)
		|| !(is_ext (arg, ".abf") || is_ext (arg, ".esb") || is_ext (arg, ".mbs") || is_ext (arg, ".nsb")))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	u8 *pl = 0;
	enumError err = ERR_NOTHING_TO_DO;
	const u32 dsz = GetDecompressedSizeMuramasaFcmp (raw, sz);
	if (!dsz || dsz > 0x10000000 || !IsMuramasaFcmp (raw, sz, sz))
		goto done;
	pl = MALLOC (dsz);
	if (!pl || DecompressMuramasaFcmp (raw, sz, pl, dsz, 0, arg, true) != ERR_OK)
		goto done;
	{
		char out[PATH_MAX];
		snprintf (out, sizeof (out), "%s.dec", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE MURAMASA-FCMP:%s (%u bytes) -> %s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, dsz, out);
		if (testmode)
		{
			err = ERR_OK;
			goto done;
		}
		File_t F;
		err = CreateFileOpt (&F, true, out, false, arg);
		if (!F.f)
			goto done;
		if (fwrite (pl, 1, dsz, F.f) != dsz)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", dsz, out);
		const enumError rerr = ResetFile (&F, opt_preserve);
		if (!err)
			err = rerr;
	}
	if (!err && dsz >= 8 && (!memcmp (pl, "MLIB", 4) || !memcmp (pl, "EMBP", 4)))
	{
		char out[PATH_MAX];
		snprintf (out, sizeof (out), "%s.strings.txt", arg);
		File_t F;
		err = CreateFileOpt (&F, true, out, false, arg);
		if (!F.f)
			goto done;
		size_t p = 0;
		while (p < dsz)
		{
			if (pl[p] < 0x20)
			{
				p++;
				continue;
			}
			const size_t s = p;
			cucp q = pl + p, end = pl + dsz;
			bool ok = true;
			while (q < end && *q)
			{
				const int c = ScanShiftJISCharE (&q, end);
				if (c < 0x20 && c != 0)
					ok = false;
				if (c <= 0)
				{
					ok = false;
					if (q < end)
						q++;
				}
			}
			const size_t n = (size_t)(q - (pl + s));
			if (ok && q < end && n >= 3)
			{
				fprintf (F.f, "%zu\t", s);
				flw0_print_sjis (F.f, pl + s, n);
				fputc ('\n', F.f);
			}
			p = s + (q < end ? n + 1 : n);
			if (p <= s)
				p = s + 1;
		}
		const enumError rerr = ResetFile (&F, opt_preserve);
		if (!err)
			err = rerr;
	}
done:
	FREE (pl);
	FREE (raw);
	return err;
}
