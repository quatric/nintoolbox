// Igor: The Game voice-over cue markers (".mkr" next to the "audio/playvo/*.fsb" banks; little endian): {u32 count, then count x {f32 time in seconds, u32 name length, name bytes}} (the
// 4 byte files hold count 0). Written as "<file>.tsv" (time<TAB>name, 269 files).
static enumError extract_igor_mkr_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".mkr") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 4)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 n = rd_le32 (raw);
	size_t p = 4;
	bool ok = n < 100000;
	for (u32 i = 0; ok && i < n; i++)
	{
		if (p + 8 > sz)
		{
			ok = false;
			break;
		}
		const u32 l = rd_le32 (raw + p + 4);
		if (l == 0 || l > 64 || p + 8 + l > sz)
		{
			ok = false;
			break;
		}
		for (u32 k = 0; k < l; k++)
			ok = ok && raw[p + 8 + k] >= 0x20 && raw[p + 8 + k] < 0x7f;
		p += 8 + l;
	}
	if (!ok || p != sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	p = 4;
	for (u32 i = 0; i < n; i++)
	{
		const u32 u = rd_le32 (raw + p), l = rd_le32 (raw + p + 4);
		float f;
		memcpy (&f, &u, 4);
		sumo_printf (&b, "%.9g\t%.*s\n", (double)f, (int)l, (ccp)raw + p + 8);
		p += 8 + l;
	}
	FREE (raw);
	enumError err = ERR_NOTHING_TO_DO;
	if (b.ok && b.n && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".tsv");
		else
			strcat (name, ".tsv");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT IGOR-MKR:%s (%u markers) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
