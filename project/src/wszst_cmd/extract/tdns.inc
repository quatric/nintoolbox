// "TDNS" sound-name tables (".bin": Super Monkey Ball: Step & Roll sound_def.bin; Wii, big endian): magic, version @4, entry count @0x10, then {id, string offset} pairs from
// 0x14 pointing at NUL-terminated names. Written as "<name>.txt" with one "id name" line per entry.
static enumError extract_tdns_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".bin") || !file_has_magic (arg, "TDNS", 4))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t count = sz >= 0x14 ? rd_be32 (raw + 0x10) : 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (count && count < 0x100000 && 0x14 + count * 8 <= sz)
	{
		char *txt = CALLOC (count, 160);
		size_t n = 0;
		bool ok = txt != 0;
		for (size_t i = 0; i < count && ok; i++)
		{
			const size_t off = rd_be32 (raw + 0x18 + 8 * i);
			if (off >= sz || !memchr (raw + off, 0, sz - off) || strlen ((ccp)raw + off) > 120)
				ok = false;
			else
				n += snprintf (txt + n, 160, "%u %s\n", (uint)rd_be32 (raw + 0x14 + 8 * i), (ccp)raw + off);
		}
		if (ok)
		{
			char dest[PATH_MAX], name[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (name, '.');
			if (dot) strcpy (dot, ".txt");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT TDNS:%s (%zu names) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, count, dest, name);
			err = shade_write (arg, dest, basedir, name, (u8 *)txt, (uint)n);
		}
		FREE (txt);
	}
	FREE (raw);
	return err;
}
