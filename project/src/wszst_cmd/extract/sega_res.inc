// Sega resource containers (".urs"/".amf"/".gdr"/".gdn"/".arm"/... from Sakura Wars: So Long, My Love; Wii, little endian): 4-byte magic (URES, AMPP, GDEN, SASN), payload
// size @4, header size @8, flags @0xc, member count @0x10, table offset @0x14 (0x1c), then a u32 offset table (0 = absent; the first member sits right after the header).
// Every member is a chunk {magic, payload size @4, header size @8, ...} (HMDL, HTEX, HMOT, HSPR, GRO3, ABDA, ABRS, UMDL, ...) spanning header + payload. Written as
// "NN_<magic>.<lowercase magic>", so HTEX members (".htx") go straight to the existing HTEX texture decoder.
static enumError extract_sega_res_file (ccp arg, ccp basedir, uint depth)
{
	if (!file_has_magic (arg, "URES", 4) && !file_has_magic (arg, "AMPP", 4) && !file_has_magic (arg, "GDEN", 4) && !file_has_magic (arg, "SASN", 4))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t hdr = sz >= 0x20 ? rd_le32 (raw + 8) : 0, cnt = sz >= 0x20 ? rd_le32 (raw + 0x10) : 0, to = sz >= 0x20 ? rd_le32 (raw + 0x14) : 0;
	bool ok = hdr >= 0x20 && hdr < sz && cnt && cnt <= 0x1000 && to == 0x1c && to + cnt * 4 <= hdr;
	size_t prev = 0, n = 0;
	for (size_t i = 0; ok && i < cnt; i++)
	{
		const size_t o = rd_le32 (raw + to + 4 * i);
		if (!o)
			continue;
		if (o < hdr || o + 0x10 > sz || o < prev)
			ok = false;
		prev = o;
		n++;
	}
	if (!ok || !n)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SEGA-RES:%s (%zu members) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
	enumError err = ERR_OK;
	for (size_t i = 0; i < cnt && !err; i++)
	{
		const size_t o = rd_le32 (raw + to + 4 * i);
		if (!o)
			continue;
		size_t e = o + rd_le32 (raw + o + 8) + rd_le32 (raw + o + 4);
		if (e > sz || e <= o)
			e = sz;
		char mg[5] = { 0 }, name[48];
		for (uint k = 0; k < 4; k++)
		{
			const u8 c = raw[o + k];
			mg[k] = c >= 'A' && c <= 'Z' ? (char)(c + 32) : c >= '0' && c <= '9' ? (char)c : c >= 'a' && c <= 'z' ? (char)c : '_';
		}
		snprintf (name, sizeof (name), "%02zu_%s.%s", i, mg, !strcmp (mg, "htex") ? "htx" : mg);
		err = shade_write (arg, dest, basedir, name, raw + o, (uint)(e - o));
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
