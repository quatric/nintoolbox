// Midway text tables (".txt" members of TNA Impact! WADs: MENUTEXT*, SUBTITLETEXT*; big endian): u32 0, u32 flags, u32 0, u32 count @0xc, then count absolute u32 offsets from 0x10
// (the first equals 0x10 + 4 * count) to NUL-terminated strings (Latin-1; the Japanese tables use another code page and are written unconverted). Written as
// "<name>.d/<name>.utf8.txt" with one "index<TAB>text" line per string, Latin-1 converted to UTF-8.
static enumError extract_midway_txt_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".txt"))
		return ERR_NOTHING_TO_DO;
	u8 hd[16];
	FILE *f = fopen (arg, "rb");
	const bool got = f && fread (hd, 1, 16, f) == 16;
	if (f)
		fclose (f);
	if (!got || rd_be32 (hd) || rd_be32 (hd + 8))
		return ERR_NOTHING_TO_DO;
	const size_t count = rd_be32 (hd + 12);
	if (!count || count > 0x20000)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const size_t tend = 0x10 + 4 * count;
	bool ok = tend < sz && rd_be32 (raw + 0x10) == tend;
	for (size_t i = 0; ok && i < count; i++)
	{
		const size_t o = rd_be32 (raw + 0x10 + 4 * i);
		if (o < tend || o >= sz || (i && o < rd_be32 (raw + 0x10 + 4 * i - 4)))
			ok = false;
	}
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	const bool ja = strlen (base) > 8 && !strncasecmp (base + strlen (base) - 6, "JA.txt", 6);
	char *txt = ok ? CALLOC (sz * 2 + count * 12 + 16, 1) : 0;
	if (txt)
	{
		size_t n = 0;
		for (size_t i = 0; i < count; i++)
		{
			n += (size_t)sprintf (txt + n, "%zu\t", i);
			for (size_t p = rd_be32 (raw + 0x10 + 4 * i); p < sz && raw[p]; p++)
			{
				const u8 c = raw[p];
				if (c == '\n')
				{
					txt[n++] = '\\';
					txt[n++] = 'n';
				}
				else if (c < 0x80 || ja)
					txt[n++] = (char)c;
				else
				{
					txt[n++] = (char)(0xc0 | c >> 6);
					txt[n++] = (char)(0x80 | (c & 0x3f));
				}
			}
			txt[n++] = '\n';
		}
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		snprintf (name, sizeof (name), "%s", base);
		char *dot = strrchr (name, '.');
		if (dot) strcpy (dot, ".utf8.txt");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT MIDWAY-TXT:%s (%zu strings) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, count, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)txt, (uint)n);
	}
	FREE (txt);
	FREE (raw);
	return err;
}
