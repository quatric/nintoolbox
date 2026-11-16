// Sukeban Shachou Rena Wii (Wii) event lists ("scr/event.dat", "lyt/event.dat"): 34-byte records {char name[32] (NUL padded, e.g. "st1001"
// or "st1004SelMG"), u8 flag (0/1), u8 group (1..)}, naming the per-event script / layout resources. Written to "<file>.tsv" (name, flag, group).
static enumError extract_sukebanevent_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	if (strcmp (base, "event.dat"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	bool good = sz >= 34 && sz % 34 == 0;
	const size_t n = sz / 34;
	for (size_t i = 0; good && i < n; i++)
	{
		const u8 *r = raw + i * 34;
		if (!isalpha (r[0]) || r[31] || r[32] > 1 || !r[33] || r[33] > 32)
			good = false;
		bool z = false;
		for (uint k = 0; good && k < 32; k++)
		{
			if (!r[k])
				z = true;
			else if (z || !isalnum (r[k]))
				good = false;
		}
	}
	if (good)
	{
		pcr_ctx_t cx;
		memset (&cx, 0, sizeof (cx));
		cx.d = raw;
		cx.sz = sz;
		cx.ok = true;
		pcr_emit (&cx, "name\tflag\tgroup\n");
		for (size_t i = 0; i < n; i++)
			pcr_emit (&cx, "%s\t%u\t%u\n", (ccp)raw + i * 34, raw[i * 34 + 32], raw[i * 34 + 33]);
		if (cx.ok)
		{
			char dest[PATH_MAX];
			snprintf (dest, sizeof (dest), "%s.tsv", arg);
			if (testmode)
			{
				fprintf (stdlog, "WOULD EXTRACT EVENTDAT:%s -> TSV:%s\n", arg, dest);
				err = ERR_OK;
			}
			else
			{
				File_t F;
				err = CreateFileOpt (&F, true, dest, false, arg);
				if (F.f && fwrite (cx.out, 1, cx.len, F.f) != cx.len)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)cx.len, dest);
				ResetFile (&F, opt_preserve);
				if (verbose >= 0 && !err)
					fprintf (stdlog, "%sEXTRACT EVENTDAT:%s -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, dest);
			}
		}
		FREE (cx.out);
	}
	FREE (raw);
	return err;
}
