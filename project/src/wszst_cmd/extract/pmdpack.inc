// Pokemon Mystery Dungeon: Explorers pack files ("dungeon.bin", "monster.bin", "m_attack.bin", ...; little endian): u32 0, u32 count, count {u32 offset, u32 size}
// records, an all-zero record, 0xff padding, then the members in record order. Written as "<file>.d/NNNN.<kind>" (at4px / pkdpx / sir0 / bin by magic); the PX
// members then decompress on their own (DecodePX).
static enumError extract_pmdpack_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	ccp ext = strrchr (arg, '.');
	if (!ext || strcasecmp (ext, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	const size_t n = sz >= 16 ? rd_le32 (raw + 4) : 0, tab = 8 + 8 * n;
	if (rd_le32 (raw) || n < 2 || n > 100000 || tab + 8 > sz || rd_le32 (raw + tab) || rd_le32 (raw + tab + 4))
		goto done;
	for (size_t i = 0, prev = tab + 8; i < n; i++)
	{
		const size_t o = rd_le32 (raw + 8 + 8 * i), s = rd_le32 (raw + 12 + 8 * i);
		if (!s)
			continue;
		if (o < prev || o > sz || s > sz - o)
			goto done;
		prev = o + s;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PMD-PACK:%s (%zu members) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
	err = ERR_OK;
	for (size_t i = 0; i < n && !err && !testmode; i++)
	{
		const size_t o = rd_le32 (raw + 8 + 8 * i), s = rd_le32 (raw + 12 + 8 * i);
		if (!s)
			continue;
		const u8 *p = raw + o;
		ccp kind = s >= 5 && !memcmp (p, "AT4PX", 5) ? "at4px" : s >= 5 && !memcmp (p, "PKDPX", 5) ? "pkdpx" : s >= 4 && !memcmp (p, "SIR0", 4) ? "sir0" : "bin";
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%04zu.%s", dest, basedir ? basedir : "", i, kind);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && fwrite (p, 1, s, F.f) != s)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", s, path);
		ResetFile (&F, opt_preserve);
	}
done:
	FREE (raw);
	return err;
}
