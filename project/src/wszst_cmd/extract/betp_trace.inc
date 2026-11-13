// "BETP" dance trace paths (".bin": The Smurfs: Dance Party "*_trace.bin"; Wii, big endian): magic, point count @4, screen width @8, height @0xc, then count records of
// {s32 move id (sign marks the stroke direction), u16 x, u16 y}. Written as "<name>.csv" with "index,id,x,y" rows.
static enumError extract_betp_trace_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".bin") || !file_has_magic (arg, "BETP", 4))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t n = sz >= 0x10 ? rd_be32 (raw + 4) : 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (n && n < 0x1000000 && sz == 0x10 + n * 8)
	{
		char *txt = CALLOC (n + 1, 48);
		size_t len = txt ? (size_t)snprintf (txt, 48, "# %ux%u\nindex,id,x,y\n", (uint)rd_be32 (raw + 8), (uint)rd_be32 (raw + 12)) : 0;
		for (size_t i = 0; txt && i < n; i++)
			len += snprintf (txt + len, 48, "%zu,%d,%u,%u\n", i, (int)rd_be32 (raw + 0x10 + 8 * i), (uint)rd_be16 (raw + 0x14 + 8 * i), (uint)rd_be16 (raw + 0x16 + 8 * i));
		if (txt)
		{
			char dest[PATH_MAX], name[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (name, '.');
			if (dot) strcpy (dot, ".csv");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT BETP:%s (%zu points) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest, name);
			err = shade_write (arg, dest, basedir, name, (u8 *)txt, (uint)len);
		}
		FREE (txt);
	}
	FREE (raw);
	return err;
}
