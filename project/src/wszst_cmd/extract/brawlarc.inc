// Super Smash Bros. Brawl fighter packs ("FitXXX00.pcs", LZ10-compressed, then this container): {"ARC\0", be16 0x0101, be16 count, ..., char name[16] at 0x10}, 0x40-byte header, then count members each with a 0x20-byte header {be16 type, be16 group, be32 size, ...} followed by `size` bytes (BRRES for types 1-3). Members are written to "<file>.d/NNN_tT.bin" and recursed.
static enumError extract_brawlarc_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (is_ext (arg, ".pac")) // wszst's own PAC extractor restores the member names
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x80 || memcmp (raw, "ARC\0", 4) || rd_be16 (raw + 4) != 0x0101)
		goto done;
	{
		const uint count = rd_be16 (raw + 6);
		if (!count || count > 4096)
			goto done;
		size_t p = 0x40;
		for (uint i = 0; i < count; i++)
		{
			if (p + 0x20 > sz || (u64)p + 0x20 + rd_be32 (raw + p + 4) > sz)
				goto done;
			p += 0x20 + rd_be32 (raw + p + 4);
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT BRAWLARC:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, count, dest);
		err = ERR_OK;
		p = 0x40;
		for (uint i = 0; i < count && !err && !testmode; i++)
		{
			const u32 len = rd_be32 (raw + p + 4);
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%03u_t%u.bin", dest, i, rd_be16 (raw + p));
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (raw + p + 0x20, 1, len, F.f) != len)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", len, path);
			ResetFile (&F, opt_preserve);
			p += 0x20 + len;
		}
		if (!err && !testmode)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (raw);
	return err;
}
