// Dokapon Kingdom "GAME.PAH" index + "GAME.PAC" data pair (Sting, Wii). The index is little-endian: u32 entry
// count, u32 offset of the entry table (0x70), then count 16-byte entries {u32 offset, u32 size, u32 0, u32 name
// offset} where the offset points into GAME.PAC and the name is a NUL-terminated string stored inside the index.
// The data file is hundreds of megabytes, so members are streamed.
static enumError extract_pah_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".pah"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 8 ? get_le32 (raw) : 0, tab = sz >= 8 ? get_le32 (raw + 4) : 0;
	char pac[PATH_MAX];
	snprintf (pac, sizeof (pac), "%s", arg);
	char *dot = strrchr (pac, '.');
	FILE *in = 0;
	if (dot && count && count <= 0x100000 && tab >= 8 && tab + 16 * (u64)count <= sz)
	{
		strcpy (dot, ".PAC");
		in = fopen (pac, "rb");
		if (!in)
		{
			strcpy (dot, ".pac");
			in = fopen (pac, "rb");
		}
	}
	u64 fsz = 0;
	if (in && !fseek (in, 0, SEEK_END))
		fsz = (u64)ftello (in);
	bool ok = in && fsz;
	for (uint i = 0; ok && i < count; i++)
	{
		const u8 *e = raw + tab + 16 * (size_t)i;
		const u64 off = get_le32 (e), n = get_le32 (e + 4), no = get_le32 (e + 12);
		ok = off + n <= fsz && !get_le32 (e + 8) && no >= tab && no < sz && memchr (raw + no, 0, sz - no)
			&& valid_sarc_path ((ccp)raw + no);
	}
	if (!ok)
	{
		if (in)
			fclose (in);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PAH:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);
	enumError err = ERR_OK;
	for (uint i = 0; i < count && !err && !testmode; i++)
	{
		const u8 *e = raw + tab + 16 * (size_t)i;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s", dest, (ccp)raw + get_le32 (e + 12));
		err = write_abe_member (in, get_le32 (e), get_le32 (e + 4), path, arg);
	}
	fclose (in);
	FREE (raw);
	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}
