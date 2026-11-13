// "iPck" index packs (".bin": Tales of Symphonia: Dawn of the New World LVDPack.bin; Wii, big endian): magic, entry count @4, table offset @8 (0x10), data offset @0xc, then
// a u32 offset table (relative to the data offset; 0xffffffff = absent, the rest ascend); entry i spans [off[i], next present offset) and the last entry runs to the end of the file. Written as "NNNN.bin".
static enumError extract_ipck_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bin") || !file_has_magic (arg, "iPck", 4))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t count = sz >= 0x10 ? rd_be32 (raw + 4) : 0, tab = sz >= 0x10 ? rd_be32 (raw + 8) : 0, data = sz >= 0x10 ? rd_be32 (raw + 12) : 0;
	if (!count || count > 0x1000000 || tab < 0x10 || tab + count * 4 > data || data > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	// 0xffffffff marks an absent entry; the present ones ascend.
	size_t lastoff = 0, present = 0;
	for (size_t i = 0; i < count; i++)
	{
		const u32 v = rd_be32 (raw + tab + 4 * i);
		if (v == 0xffffffffu)
			continue;
		if (v > sz - data || v < lastoff)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		lastoff = v;
		present++;
	}
	if (!present)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT IPCK:%s (%zu entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, present, dest);
	enumError err = ERR_OK;
	for (size_t i = 0; i < count && !err; i++)
	{
		const u32 v = rd_be32 (raw + tab + 4 * i);
		if (v == 0xffffffffu)
			continue;
		size_t e = sz - data;
		for (size_t j = i + 1; j < count; j++)
			if (rd_be32 (raw + tab + 4 * j) != 0xffffffffu)
			{
				e = rd_be32 (raw + tab + 4 * j);
				break;
			}
		const size_t o = v;
		char name[32];
		snprintf (name, sizeof (name), "%04zu.bin", i);
		err = shade_write (arg, dest, basedir, name, raw + data + o, (uint)(e - o));
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
