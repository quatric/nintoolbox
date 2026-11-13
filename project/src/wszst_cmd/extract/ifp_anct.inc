// Rockstar Leeds animation packages (Manhunt 2 ".ifp", after MH2Z unpacking; big endian): "ANCT" u32, "BLOC" {u32 length, block name}, then ANPK animation packs, each starting
// "ANPK" u32 size, "NAME" {u32 length, animation name}, followed by DGAN/CPAN/SEQT keyframe data (not decoded). The block name and every animation name are written as a text list
// "<name>.ifp.d/<name>.txt".
static enumError extract_ifp_anct_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".ifp"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x30 || memcmp (c, "ANCT", 4) || memcmp (c + 8, "BLOC", 4))
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	const uint bl = rd_be32 (c + 12);
	if (bl && bl < 128 && 16 + bl <= cs)
		sumo_printf (&b, "block %.*s\n", (int)bl, (ccp)c + 16);
	uint n = 0;
	for (size_t p = 16; p + 16 < cs; p++)
	{
		if (memcmp (c + p, "ANPK", 4) || memcmp (c + p + 8, "NAME", 4))
			continue;
		const uint nl = rd_be32 (c + p + 12);
		if (!nl || nl > 120 || p + 16 + nl > cs)
			continue;
		sumo_printf (&b, "%.*s\n", (int)nl, (ccp)c + p + 16);
		n++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (b.ok && n && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".txt");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT IFP-ANCT:%s (%u animations) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	FREE (c);
	return err;
}
