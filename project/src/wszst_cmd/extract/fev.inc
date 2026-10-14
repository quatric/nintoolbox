// FMOD Designer event files (".fev", "FEV1", little-endian: u32 version, u32 sizes, u32 count, project name ...)
// as shipped with FMOD Ex games. The records between the strings are version dependent and not interpreted;
// every length-prefixed string {u32 length including the NUL, chars, NUL} of printable ASCII is listed in decode
// mode as "offset<TAB>text" lines in "<file>.strings.txt": the project, bank, category, event group, event and
// wave bank names.
static enumError extract_fev_file (ccp arg)
{
	if ((export_count <= 0 && !opt_decode) || !is_ext (arg, ".fev"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x20 || memcmp (raw, "FEV1", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char out[PATH_MAX];
	snprintf (out, sizeof (out), "%s.strings.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE FEV:%s -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, out);
	if (testmode)
	{
		FREE (raw);
		return ERR_OK;
	}
	File_t F;
	enumError err = CreateFileOpt (&F, true, out, false, arg);
	for (size_t p = 0x10; F.f && p + 8 <= sz;)
	{
		const size_t n = rd_le32 (raw + p);
		bool ok = n >= 2 && n <= 256 && p + 4 + n <= sz && !raw[p + 3 + n];
		for (size_t k = 0; ok && k + 1 < n; k++)
			ok = raw[p + 4 + k] >= 0x20 && raw[p + 4 + k] < 0x7f;
		if (ok)
		{
			fprintf (F.f, "%zu\t%s\n", p + 4, raw + p + 4);
			p += 4 + n;
		}
		else
			p++;
	}
	if (F.f)
	{
		const enumError rerr = ResetFile (&F, opt_preserve);
		if (!err)
			err = rerr;
	}
	FREE (raw);
	return err;
}
