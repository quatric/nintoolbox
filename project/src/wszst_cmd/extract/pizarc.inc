// Family GameShow (Wii) "*.piz" archives: little-endian {"PIZ\0", u32 version 3, u32 count, u32 0xcccccccc}, zero padded to 0x800, then count * 0x80-byte entries {char name[0x74], u32 byte offset, u32 size, u32 hash}.
// Members (RenderWare "WIIDFF" models, "WIITXD" texture dictionaries, "WIIANM"/"WIIUVA" animations) are stored raw and written to "<file>.d/<name>".
static enumError extract_pizarc_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".piz") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x880 || memcmp (raw, "PIZ\0", 4))
		goto done;
	{
		const u32 count = rd_le32 (raw + 8);
		if (!count || count > 100000 || 0x800 + (u64)count * 0x80 > sz)
			goto done;
		for (uint i = 0; i < count; i++)
		{
			const u8 *e = raw + 0x800 + (size_t)i * 0x80;
			if (!e[0] || memchr (e, 0, 0x74) == 0 || (u64)rd_le32 (e + 0x74) + rd_le32 (e + 0x78) > sz)
				goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT PIZ:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, count, dest);
		err = ERR_OK;
		for (uint i = 0; i < count && !err && !testmode; i++)
		{
			const u8 *e = raw + 0x800 + (size_t)i * 0x80;
			char name[0x80];
			uint k = 0;
			for (; k < 0x74 && e[k]; k++)
				name[k] = e[k] == '/' || e[k] == '\\' || e[k] < 0x20 ? '_' : e[k];
			name[k] = 0;
			if (!strcmp (name, ".") || !strcmp (name, ".."))
				name[0] = '_';
			const u32 off = rd_le32 (e + 0x74), len = rd_le32 (e + 0x78);
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s", dest, name);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (raw + off, 1, len, F.f) != len)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", len, path);
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
