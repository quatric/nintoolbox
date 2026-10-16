// Krome "RKV2" archives ".rkv" (Star Wars: The Force Unleashed, Wii; little-endian). The file starts with two identical
// 0x40-byte headers: "RKV2", u32 count, u32 name table related value, u32 count, u32, u32 toc offset (0x14), u32 toc
// size (0x18), ... The table at toc offset holds count 20 byte records {u32 name offset, u32 0, u32 size, u32 data
// offset, u32 name hash} followed by the NUL separated names; members are stored plain. Written under "<archive>.d/",
// streamed straight from the archive.
static enumError extract_rkv2_file (ccp arg, ccp basedir, uint depth)
{
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 head[0x40];
	if (fread (head, 1, sizeof (head), f) != sizeof (head) || memcmp (head, "RKV2", 4))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	fseeko (f, 0, SEEK_END);
	const u64 file_size = (u64)ftello (f);
	const u32 count = rd_le32 (head + 4), toc_off = rd_le32 (head + 0x14), toc_size = rd_le32 (head + 0x18);
	if (!count || count > 0x100000 || (u64)toc_off + toc_size > file_size || toc_size < (u64)count * 20 + 2)
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	u8 *toc = MALLOC (toc_size);
	if (!toc || fseeko (f, (off_t)toc_off, SEEK_SET) || fread (toc, 1, toc_size, f) != toc_size)
	{
		FREE (toc);
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	const u8 *names = toc + (size_t)count * 20;
	const size_t names_size = toc_size - (size_t)count * 20;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT RKV2:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
			arg, count, dest);

	enumError err = ERR_OK;
	u8 *buf = testmode ? 0 : MALLOC (1 << 20);
	for (uint i = 0; i < count && !err; i++)
	{
		const u8 *r = toc + (size_t)i * 20;
		const u32 noff = rd_le32 (r), size = rd_le32 (r + 8), off = rd_le32 (r + 12);
		char name[256];
		if (noff < names_size && memchr (names + noff, 0, names_size - noff) && strlen ((ccp)names + noff) < sizeof (name))
			snprintf (name, sizeof (name), "%s", names + noff);
		else
			snprintf (name, sizeof (name), "%05u.bin", i);
		for (char *c = name; *c; c++)
			if (*c == '\\')
				*c = '/';
		if (!valid_sarc_path (name) || (u64)off + size > file_size)
		{
			if ((u64)off + size > file_size)
				continue;
			snprintf (name, sizeof (name), "%05u.bin", i);
		}
		if (testmode)
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (!err && F.f && buf && !fseeko (f, (off_t)off, SEEK_SET))
		{
			u32 left = size;
			while (left)
			{
				const size_t n = fread (buf, 1, left < (1 << 20) ? left : (1 << 20), f);
				if (!n || fwrite (buf, 1, n, F.f) != n)
				{
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
					break;
				}
				left -= (u32)n;
			}
		}
		ResetFile (&F, opt_preserve);
	}
	FREE (buf);
	FREE (toc);
	fclose (f);
	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}
