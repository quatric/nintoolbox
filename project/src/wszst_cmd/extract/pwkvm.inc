// Pipeworks "PWK" script files (Monster High: Ghoul Spirit, Wii): ".pvd" (debug / symbol data of a ".pvm" virtual machine module, whose bytecode is not touched) and ".mcl" (master class list), all starting with
// "PWK <kind> v<version> (big endian)   \x1a\0" followed by big-endian size / offset fields. The bytecode and the class records are not decoded; the NUL terminated identifier strings
// (source file names, "Class::method" names, local variable names, class names) are written as "<file>.txt": the header line, then "<offset>\t<string>".
static enumError extract_pwkvm_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".pvd") && !is_ext (arg, ".mcl"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	const u8 *ctl = sz >= 0x40 && !memcmp (raw, "PWK ", 4) ? memchr (raw, 0x1a, 0x40) : 0;
	if (!ctl || ctl < raw + 0x20)
		goto done;
	{
		c.d = raw;
		c.sz = sz;
		c.ok = true;
		{
			size_t hl = (size_t)(ctl - raw);
			while (hl && raw[hl - 1] == ' ')
				hl--;
			pcr_emit (&c, "%.*s\n", (int)hl, (ccp)raw);
		}
		uint n = 0;
		for (size_t p = (size_t)(ctl - raw) + 1; p < sz;)
		{
			size_t e = p;
			while (e < sz && (isalnum (raw[e]) || (raw[e] && strchr ("_:.<>@$~ -", raw[e]))))
				e++;
			if (e < sz && !raw[e] && e - p >= 3 && (isalpha (raw[p]) || raw[p] == 0x5f || raw[p] == 0x3c) && memchr ("@$", raw[p + 1], 2) == 0 && (p == 0 || raw[p - 1] < 0x20))
			{
				pcr_emit (&c, "%zu\t%.*s\n", p, (int)(e - p), (ccp)raw + p);
				n++;
			}
			p = e + 1;
		}
		if (c.ok && n)
		{
			char dest[PATH_MAX];
			snprintf (dest, sizeof (dest), "%s.txt", arg);
			if (testmode)
			{
				fprintf (stdlog, "WOULD EXTRACT PWKVM:%s -> TXT:%s\n", arg, dest);
				err = ERR_OK;
			}
			else
			{
				File_t F;
				err = CreateFileOpt (&F, true, dest, false, arg);
				if (F.f && fwrite (c.out, 1, c.len, F.f) != c.len)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)c.len, dest);
				ResetFile (&F, opt_preserve);
				if (verbose >= 0 && !err)
					fprintf (stdlog, "%sEXTRACT PWKVM:%s (%u strings) -> TXT:%s\n", verbose > 0 ? "\n" : "", arg, n, dest);
			}
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
