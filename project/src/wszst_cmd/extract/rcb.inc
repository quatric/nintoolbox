// ".rcb" resource bundle (Bass Fishing Wii / Big Catch, e.g. system/sys_lang_en.rcb; big endian): 0x40-byte header {0, 0x40, 1, ..., name
// @0x20 (bundle name)}, then members each behind a 0x40-byte header {u32 span (header + payload, rounded up to 0x20), u32 0x40, u32 payload
// size, u32 flag (1 = last member), ..., NUL-padded path @0x20 (32 bytes)}, payload right after the header (BRFNT / BRRES ...). Members are
// written under their stored paths in "<file>.d".
static enumError extract_rcb_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x80 || rd_be32 (raw) || rd_be32 (raw + 4) != 0x40 || rd_be32 (raw + 8) != 1 || memchr (raw + 0x20, 0, 0x20) == 0)
		goto done;
	uint n = 0;
	{
		u64 o = 0x40;
		while (o + 0x40 <= sz)
		{
			const u32 sp = rd_be32 (raw + o), hs = rd_be32 (raw + o + 4), pl = rd_be32 (raw + o + 8);
			if (!sp || hs != 0x40 || (sp & 0x1f) || o + hs + pl > sz || sp < hs + pl || !raw[o + 0x20] || !memchr (raw + o + 0x20, 0, 0x20))
				break;
			n++;
			o += sp;
		}
		if (!n)
			goto done;
	}
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT RCB:%s (%u members) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		u64 o = 0x40;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u32 sp = rd_be32 (raw + o), pl = rd_be32 (raw + o + 8);
			char name[0x21];
			memcpy (name, raw + o + 0x20, 0x20);
			name[0x20] = 0;
			for (char *p = name; *p; p++)
				if (*p == '\\' || *p == ':')
					*p = '/';
			const char *safe = name;
			while (!strncmp (safe, "../", 3) || *safe == '/')
				safe += *safe == '/' ? 1 : 3;
			char alt[32];
			if (!*safe)
				snprintf (alt, sizeof (alt), "%03u.bin", i), safe = alt;
			err = shade_write (arg, dest, basedir, safe, raw + o + 0x40, pl);
			o += sp;
		}
	}
done:
	FREE (raw);
	return err;
}
