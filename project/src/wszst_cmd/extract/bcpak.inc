// ".bcpak" resource pack (Top Shot Arcade, Our House - Party!: "paks/*.bcpak"): big endian {u16 0x9a3f, u16 entry count, u32 pack hash, u32 constant
// 0xbcb0b530}, then entries {u32 size, u32 offset, u32 resource hash, u32 check}; the first entry's offset ends the table. Entries are written
// as "NNN_<hash>.<ext>" in "<file>.d" (".tpl" for TPL textures, else ".bin").
static enumError extract_bcpak_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".bcpak") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x10 || rd_be16 (raw) != 0x9a3f)
		goto done;
	{
		const uint n = rd_be16 (raw + 2);
		if (n > 100000 || 0xc + 16 * (u64)n > sz)
			goto done;
		for (uint i = 0; i < n; i++)
		{
			const u8 *e = raw + 0xc + 16 * i;
			if ((u64)rd_be32 (e) + rd_be32 (e + 4) > sz || rd_be32 (e + 4) < 0xc + 16 * n)
				goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT BCPAK:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = raw + 0xc + 16 * i;
			const u32 esz = rd_be32 (e), off = rd_be32 (e + 4);
			char name[64];
			snprintf (name, sizeof (name), "%03u_%08x.%s", i, rd_be32 (e + 8), esz >= 4 && rd_be32 (raw + off) == 0x0020af30 ? "tpl" : "bin");
			err = shade_write (arg, dest, basedir, name, raw + off, esz);
		}
	}
done:
	FREE (raw);
	return err;
}
