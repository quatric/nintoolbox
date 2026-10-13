// EA Tiburon "TERF" UI resource bundles (Madden NFL, NCAA Football, NASCAR Kart Racing on Wii; "uis_*.dat",
// "ui_proj2.dat"), big-endian: "TERF", u32 header size (0x20 or 0x40), a "DIR1" chunk {tag, u32 chunk size, then
// (size - 8) / 8 {u32 offset, u32 size} pairs, unused ones zero} and a "DATA" chunk {tag, u32 size, ...} right behind
// it. Offsets are relative to the start of the DATA chunk. The members are EA "SHPG" shape archives and are
// written as "NNNN.gsh" (other content as "NNNN.bin") so that the usual decoders turn them into PNGs. The names of
// the members are not stored.
static enumError extract_terf_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".dat"))
		return ERR_NOTHING_TO_DO;
	FILE *in = fopen (arg, "rb");
	if (!in)
		return ERR_NOTHING_TO_DO;
	u8 hdr[0x10];
	if (!read_abe_at (in, 0, hdr, sizeof (hdr)) || memcmp (hdr, "TERF", 4) || fseek (in, 0, SEEK_END))
	{
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}
	const long end = ftell (in);
	const u64 fsz = end < 0 ? 0 : (u64)end;
	const u64 hs = rd_be32 (hdr + 4);
	u8 dh[8];
	if (hs < 0x10 || hs > 0x100 || !read_abe_at (in, hs, dh, sizeof (dh)) || memcmp (dh, "DIR1", 4))
	{
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}
	const u64 dsz = rd_be32 (dh + 4), data = hs + dsz;
	u8 th[8];
	if (dsz < 16 || dsz > 0x100000 || data + 8 > fsz || !read_abe_at (in, data, th, sizeof (th)) || memcmp (th, "DATA", 4))
	{
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = (uint)((dsz - 8) / 8);
	u8 *tab = MALLOC (8 * (size_t)count);
	if (!tab || !read_abe_at (in, hs + 8, tab, 8 * (size_t)count))
	{
		FREE (tab);
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}
	uint used = 0;
	for (uint i = 0; i < count; i++)
	{
		const u64 o = rd_be32 (tab + 8 * (size_t)i), n = rd_be32 (tab + 8 * (size_t)i + 4);
		if (!o && !n)
			continue;
		if (data + o + n > fsz)
		{
			FREE (tab);
			fclose (in);
			return ERR_NOTHING_TO_DO;
		}
		used += n > 0;
	}
	if (!used)
	{
		FREE (tab);
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT TERF:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg,
			used, dest);
	enumError err = ERR_OK;
	for (uint i = 0; i < count && !err && !testmode; i++)
	{
		const u64 o = rd_be32 (tab + 8 * (size_t)i), n = rd_be32 (tab + 8 * (size_t)i + 4);
		if (!n)
			continue;
		u8 magic[4] = {0};
		read_abe_at (in, data + o, magic, sizeof (magic));
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%04u.%s", dest, i, memcmp (magic, "SHPG", 4) ? "bin" : "gsh");
		err = write_abe_member (in, data + o, (u32)n, path, arg);
	}
	FREE (tab);
	fclose (in);
	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}
