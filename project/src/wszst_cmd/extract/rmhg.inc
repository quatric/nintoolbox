// Grasshopper Manufacture "RMHG" resource container (No More Heroes 1/2 ".rsl": "CHR/TRV001.rsl" ...): little endian {"RMHG", u32 kind (1 =
// textures, 2 = models, 3 = group, 0xcd ...), u32 0x20 (header size), u32 0x4xx build number, 16 zero bytes}, then 32 byte entries {u32 offset (from the
// container start), u32 size, u32 1 when nested, u32 0x427, u32 id, 12 zero bytes}; the first entry's offset marks the end of the entry
// table. An entry holds either another RMHG (extracted as a sub directory "NNN_<id>/") or a leaf (GCT0 texture -> ".bin", GMF2 model -> ".gmf2", GAN2 animation -> ".gan2",
// anything else ".dat"), written as "NNN_<id>.<ext>" below "<file>.d".
static bool rmhg_ok (const u8 *p, size_t n)
{
	return n >= 0x20 && !memcmp (p, "RMHG", 4) && rd_le32 (p + 8) == 0x20 && (rd_le32 (p + 12) >> 8) == 4;
}

static enumError rmhg_walk (ccp arg, ccp dest, ccp basedir, const u8 *p, size_t n, ccp prefix, uint level)
{
	const u32 first = n >= 0x24 ? rd_le32 (p + 0x20) : 0;
	if (!first)
		return ERR_OK;
	if (first < 0x40 || first > n || (first - 0x20) % 32 || level > 6)
		return ERR_NOTHING_TO_DO;
	const uint cnt = (first - 0x20) / 32;
	for (uint i = 0; i < cnt; i++)
	{
		const u8 *e = p + 0x20 + 32 * i;
		const u32 off = rd_le32 (e), sz = rd_le32 (e + 4);
		if ((u64)off + sz > n || (sz && off < first))
			return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_OK;
	for (uint i = 0; i < cnt && !err; i++)
	{
		const u8 *e = p + 0x20 + 32 * i;
		const u32 off = rd_le32 (e), sz = rd_le32 (e + 4), id = rd_le32 (e + 16);
		const u8 *d = p + off;
		char name[PATH_MAX];
		if (rmhg_ok (d, sz))
		{
			snprintf (name, sizeof (name), "%s%03u_%x/", prefix, i, id);
			if (rmhg_walk (arg, dest, basedir, d, sz, name, level + 1))
				err = ERR_NOTHING_TO_DO;
			continue;
		}
		ccp ext = sz >= 4 && !memcmp (d, "GCT0", 4) ? "bin" : sz >= 4 && !memcmp (d, "GMF2", 4) ? "gmf2" : sz >= 4 && !memcmp (d, "GAN2", 4) ? "gan2" : "dat";
		snprintf (name, sizeof (name), "%s%03u_%x.%s", prefix, i, id, ext);
		err = shade_write (arg, dest, basedir, name, d, sz);
	}
	return err;
}

static enumError extract_rmhg_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (rmhg_ok (raw, sz))
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (testmode)
			fprintf (stdlog, "WOULD EXTRACT RMHG:%s -> %s/\n", arg, dest);
		else
		{
			err = rmhg_walk (arg, dest, basedir, raw, sz, "", 0);
			if (!err && verbose >= 0)
				fprintf (stdlog, "%sEXTRACT RMHG:%s -> %s/\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (raw);
	return err;
}
