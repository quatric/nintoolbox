// 10 Minute Solution (Wii, Europe) "<lang>/FiveMinFit.fmod" FMOD Designer event name table, big-endian: 8 zero bytes, then strings stored as {u16 length, UTF-16BE characters}:
// the project file name ("FiveMinFit.fev"), a u16 count and that many name components (event groups, banks and events such as "snd", "gme_fx", "sfx_mnu_selectionclick").
// A binary tail (event / group records that index the strings, about 30 KB) follows and is not decoded. Written as "names.txt" (the project name, then "<index>\t<string>").
static ccp fmodstr_read (const u8 *d, size_t sz, size_t *p, char *out, size_t outsz)
{
	if (*p + 2 > sz)
		return 0;
	const size_t n = rd_be16 (d + *p);
	if (!n || *p + 2 + 2 * n > sz || n * 3 + 1 > outsz)
		return 0;
	size_t o = 0;
	for (size_t i = 0; i < n; i++)
	{
		const uint c = rd_be16 (d + *p + 2 + 2 * i);
		if (c < 0x20 || c > 0x7e)
			return 0;
		out[o++] = (char)c;
	}
	out[o] = 0;
	*p += 2 + 2 * n;
	return out;
}

static enumError extract_fmodstr_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".fmod"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	char *t = 0;
	if (sz < 0x40 || rd_be32 (raw) || rd_be32 (raw + 4))
		goto done;
	{
		char s[256];
		size_t p = 8;
		if (!fmodstr_read (raw, sz, &p, s, sizeof (s)) || strlen (s) < 5 || strcasecmp (s + strlen (s) - 4, ".fev") || p + 2 > sz)
			goto done;
		const uint cnt = rd_be16 (raw + p);
		p += 2;
		if (!cnt)
			goto done;
		const size_t cap = 64 + sz * 2 + (size_t)cnt * 16;
		t = MALLOC (cap);
		size_t tl = snprintf (t, cap, "%s\n", s);
		for (uint i = 0; i < cnt; i++)
		{
			if (!fmodstr_read (raw, sz, &p, s, sizeof (s)))
				goto done;
			tl += snprintf (t + tl, cap - tl, "%u\t%s\n", i, s);
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT FMODSTR:%s (%u names) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, cnt, dest);
		err = ERR_OK;
		if (!testmode)
			err = shade_write (arg, dest, basedir, "names.txt", (const u8 *)t, (uint)tl);
	}
done:
	FREE (t);
	FREE (raw);
	return err;
}
