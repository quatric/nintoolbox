// "gamedata.fat" + "gamedata" archives (Battle of Giants: Dinosaurs Strike, Wii), big-endian: the 53 KiB ".fat" is {u32 count, count * {u32 id, u32 0, u32 offset}}
// (offsets ascend, the sizes are the gaps, the last runs to the end of "gamedata" next to it); every member is one zlib stream. The decoded resource starts
// {u32 kind, u32 size, u32 type hash, u32 id (the entry id again)} followed by the payload; the ids are the numbers the game's "bink" files are
// named after ("00/1E/CC/0E.bink" = 0x1ecc0e). Members are written decoded as "<id as 8 hex digits>.res".
static enumError extract_gamedatafat_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".fat"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	FILE *dat = 0;
	if (sz < 16)
		goto done;
	{
		const u32 n = rd_be32 (raw);
		if (n < 2 || n > 1000000 || 4 + 12 * (u64)n != sz)
			goto done;
		char dn[PATH_MAX];
		snprintf (dn, sizeof (dn), "%s", arg);
		dn[strlen (dn) - 4] = 0;
		dat = fopen (dn, "rb");
		if (!dat || fseeko (dat, 0, SEEK_END))
			goto done;
		const u64 dsz = (u64)ftello (dat);
		u64 prev = 0;
		for (u32 i = 0; i < n; i++)
		{
			const u8 *e = raw + 4 + 12 * (size_t)i;
			const u64 o = rd_be32 (e + 8);
			if (rd_be32 (e + 4) || o < prev || o >= dsz)
				goto done;
			prev = o;
		}
		// the first member must be a zlib stream
		u8 z[2];
		if (fseeko (dat, 0, SEEK_SET) || fread (z, 1, 2, dat) != 2 || z[0] != 0x78)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT GAMEDATA-FAT:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (u32 i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = raw + 4 + 12 * (size_t)i;
			const u64 o = rd_be32 (e + 8), end = i + 1 < n ? rd_be32 (e + 12 + 8) : dsz;
			u8 *comp = MALLOC (end - o);
			if (!comp || fseeko (dat, (off_t)o, SEEK_SET) || fread (comp, 1, end - o, dat) != end - o)
			{
				FREE (comp);
				err = ERR_READ_FAILED;
				break;
			}
			uint osz = 0;
			size_t used = 0;
			u8 *out = fst_inflate (comp, end - o, &osz, &used);
			FREE (comp);
			if (!out)
				continue;	// not a zlib member: skipped
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%08x.res", dest, basedir ? basedir : "", rd_be32 (e));
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && osz && fwrite (out, 1, osz, F.f) != osz)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", osz, path);
			ResetFile (&F, opt_preserve);
			FREE (out);
		}
	}
done:
	if (dat)
		fclose (dat);
	FREE (raw);
	return err;
}
