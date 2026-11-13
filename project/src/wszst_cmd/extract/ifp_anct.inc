// Rockstar Leeds animation packages (Manhunt 2 ".ifp", after MH2Z unpacking; big endian): "ANCT" u32 block count, then per block "BLOC" {u32 length, block name} and an "ANPK" pack of
// animations (see mh2_anim.inc). Written into "<name>.ifp.d": "<name>.txt" lists the blocks and animations, and every animation's keyframes are decoded to "<block>/<animation>.csv".
static enumError extract_ifp_anct_file (ccp arg, ccp basedir, uint depth)
{
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
	nintendo_sarc_entry_t *entries = 0;
	uint n = 0, cap = 0, nanim = 0;
	const uint nblocks = rd_be32 (c + 4);
	size_t p = 8;
	for (uint k = 0; k < nblocks && p + 12 <= cs && !memcmp (c + p, "BLOC", 4); k++)
	{
		const uint bl = rd_be32 (c + p + 4);
		if (!bl || bl > 120 || p + 8 + bl + 8 > cs)
			break;
		char blk[128];
		snprintf (blk, sizeof blk, "%.*s", (int)bl, (ccp)c + p + 8);
		sumo_printf (&b, "block %s\n", blk);
		for (char *q = blk; *q; q++)
			if (!isalnum ((u8)*q) && *q != '_' && *q != '-')
				*q = '_';
		p += 8 + bl;
		char prefix[160];
		snprintf (prefix, sizeof prefix, "%.100s/", blk);
		const uint before = n;
		const uint got = mh2anim_pack (&entries, &n, &cap, c, cs, &p, prefix);
		nanim += got;
		for (uint i = before; i < n; i++)
			sumo_printf (&b, "  %s\n", entries[i].name);
		if (!got)
			break;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (b.ok && nanim)
	{
		char name[PATH_MAX];
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".txt");
		if (n == cap)
		{
			cap = cap ? 2 * cap : 64;
			entries = REALLOC (entries, cap * sizeof (*entries));
			if (entries)
				memset (entries + n, 0, (cap - n) * sizeof (*entries));
		}
		if (entries && OwnedEntryAdd (entries, n, name, (u8 *)b.p, (uint)b.n))
		{
			n++;
			err = write_owned_entries (arg, basedir, depth, "IFP-ANCT", entries, n);
			entries = 0;
		}
	}
	if (entries)
		ResetOwnedEntries (entries, n);
	FREE (b.p);
	FREE (c);
	return err;
}
