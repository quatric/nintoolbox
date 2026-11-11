// JParticle effect containers ".jpc" ("JPAC1-00" / "JPAC2-10": Twilight Princess, Super Mario Galaxy, Pikmin 2, Wind Waker HD sources):
// 0x20-byte header { char[8] magic, u16 resource count @8, u16 texture count @0xa, u32 resource table @0xc, u32 texture table @0x10 }.
// The texture table is a run of "TEX1" blocks {magic, u32 total size, u32 unknown, char name[0x14] @0xc, BTI image @0x20}. Every one
// is written as "<name>.bti" (a standalone BTI image the texture handlers decode); the particle resources themselves are not decoded.
static enumError extract_jpc_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".jpc") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x20 || memcmp (raw, "JPAC", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const uint ntex = raw[10] << 8 | raw[11];
	size_t p = rd_be32 (raw + 0x10);
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	uint done = 0;
	for (uint i = 0; i < ntex && p + 0x40 <= sz; i++)
	{
		const u8 *k = raw + p;
		const u32 bs = rd_be32 (k + 4);
		if (memcmp (k, "TEX1", 4) || bs < 0x40 || p + bs > sz)
			break;
		if (!done && !testmode && (verbose >= 0))
			fprintf (stdlog, "%sEXTRACT JPC:%s (%u textures) -> %s/\n", verbose > 0 ? "\n" : "", arg, ntex, dest);
		else if (!done && testmode)
			fprintf (stdlog, "WOULD EXTRACT JPC:%s (%u textures) -> %s/\n", arg, ntex, dest);
		char nm[0x20], name[0x40];
		snprintf (nm, sizeof (nm), "%.20s", k + 0xc);
		for (char *c = nm; *c; c++)
			if (*c == '/' || *c == '\\' || (u8)*c < 0x20)
				*c = '_';
		snprintf (name, sizeof (name), "%s%s.bti", nm[0] ? nm : "tex", "");
		if (!nm[0])
			snprintf (name, sizeof (name), "tex%03u.bti", i);
		err = testmode ? ERR_OK : shade_write (arg, dest, basedir, name, k + 0x20, bs - 0x20);
		done++;
		if (err)
			break;
		p += bs;
	}
	if (!done)
		err = ERR_NOTHING_TO_DO;
	FREE (raw);
	return err;
}
