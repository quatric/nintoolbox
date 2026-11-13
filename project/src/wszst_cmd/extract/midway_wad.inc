// Midway/Shell-style ".WAD" resource packs (TNA Impact!; big endian): a chain of records {name[16], type[4] ("TIM", "SPR", "PHM", "FNT", "PHA", "TXT", "GAM", "SFX", "TOC", ...),
// u32 payload size, 12 more header bytes} with the payload right after the 0x20-byte header; the chain ends at a "WEND" record. Written as "NNNN_<name>.<type>".
static bool midway_wad_name (const u8 *p)
{
	uint n = 0;
	while (n < 16 && p[n])
		n++;
	if (!n)
		return false;
	for (uint i = 0; i < n; i++)
		if (p[i] < 0x20 || p[i] > 0x7e)
			return false;
	for (uint i = n; i < 16; i++)
		if (p[i] != 0 && p[i] != 0xff)
			return false;
	return true;
}

static enumError extract_midway_wad_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".wad"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	// validate the whole chain first
	size_t p = 0;
	uint n = 0;
	bool ok = false;
	while (p + 0x20 <= sz && midway_wad_name (raw + p) && raw[p + 16] >= 'A' && raw[p + 16] <= 'Z')
	{
		const size_t a = rd_be32 (raw + p + 20);
		const bool wend = !memcmp (raw + p + 16, "WEND", 4);
		if (p + 0x20 + a > sz)
			break;
		if (wend)
		{
			ok = n > 0;
			break;
		}
		p += 0x20 + a;
		n++;
	}
	if (!ok)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT MIDWAY-WAD:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
	enumError err = ERR_OK;
	p = 0;
	for (uint i = 0; i < n && !err; i++)
	{
		const size_t a = rd_be32 (raw + p + 20);
		char nm[24], ty[8], name[64];
		snprintf (nm, sizeof (nm), "%.16s", (ccp)raw + p);
		snprintf (ty, sizeof (ty), "%.4s", (ccp)raw + p + 16);
		for (char *c = nm; *c; c++)
			if (!isalnum ((u8)*c) && *c != '_' && *c != '-')
				*c = '_';
		for (char *c = ty; *c; c++)
			*c = (char)tolower ((u8)*c);
		snprintf (name, sizeof (name), "%04u_%s.%s", i, nm, ty);
		err = shade_write (arg, dest, basedir, name, raw + p + 0x20, (uint)a);
		p += 0x20 + a;
	}
	FREE (raw);
	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}
