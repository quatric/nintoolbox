// Twinkle Queen (Wii) "Battle/*.cg" image archives, big-endian: {"DHGC", u32 payload size, u8 4 flags, char type[]"tpl" at 0x0c, ...}, 0x20-byte entries from 0x18 {u32 0, u32 0, u32 size, u32 offset, char name[16]} (the
// directory runs up to the first offset) and data at offset + 8. Members (TPL images) are carved to "<file>.d/<name>.<type>" and extracted recursively.
static enumError extract_dhgcpack_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".cg"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x60 || memcmp (raw, "DHGC", 4))
		goto done;
	{
		const u32 first = rd_be32 (raw + 0x24);
		if (first < 0x38 || first > sz || (first - 0x18) % 0x20)
			goto done;
		const uint n = (first - 0x18) / 0x20;
		char type[8] = { 0 };
		memcpy (type, raw + 0x0c, 4);
		if (!type[0])
			strcpy (type, "bin");
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT CG:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = raw + 0x18 + 0x20 * i;
			const u64 size = rd_be32 (e + 8), off = (u64)rd_be32 (e + 12) + 8;
			if (off + size > sz)
				continue;
			char name[17];
			memcpy (name, e + 16, 16);
			name[16] = 0;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s.%s", dest, basedir ? basedir : "", name[0] ? name : "unnamed", type);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (raw + off, 1, size, F.f) != size)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)size, path);
			ResetFile (&F, opt_preserve);
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
