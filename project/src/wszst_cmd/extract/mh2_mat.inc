// Rockstar Leeds material table (Manhunt 2 "mat_wii.bin", big endian): u32 header word count H (64 or 65), H words of header, then 200-byte records {u32 id, char name[196]} and a
// trailing u32 (the file size is exactly 4*H + 200*N + 4). The material names are written as a text list "<name>.txt" (the other record bytes are zero on all level files).
static enumError extract_mh2_mat_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	const char *sl = strrchr (arg, '/');
	if (strcmp (sl ? sl + 1 : arg, "mat_wii.bin"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x200)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	const size_t hdr = 4 * (size_t)rd_be32 (c);
	if (hdr < 0x100 || hdr > 0x110 || cs < hdr + 4 || (cs - hdr - 4) % 200)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	const uint n = (uint)((cs - hdr - 4) / 200);
	sumo_buf_t b = { 0, 0, 0, true };
	uint ok = 0;
	for (uint i = 0; i < n; i++)
	{
		const u8 *r = c + hdr + 200 * (size_t)i + 4;
		size_t l = 0;
		while (l < 196 && r[l] >= 32 && r[l] < 127)
			l++;
		if (l && l < 196 && !r[l])
		{
			sumo_printf (&b, "%.*s\n", (int)l, (ccp)r);
			ok++;
		}
	}
	FREE (c);
	enumError err = ERR_NOTHING_TO_DO;
	if (b.ok && ok * 10 >= n * 9 && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		snprintf (name, sizeof (name), "mat_wii.txt");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT MH2-MAT:%s (%u materials) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ok, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
