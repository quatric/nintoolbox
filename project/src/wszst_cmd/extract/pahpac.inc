// Sting "GAME.PAH" + "GAME.PAC" archives (Dokapon Kingdom, Wii), little endian: the 78 KiB ".PAH" index is {u32 entry count, 27 u32 section offsets (the
// first is 0x70, the start of the entry table, the rest point into a small hash / sort table and the name pool)}, then count * {u32 offset into the ".PAC",
// u32 size, u32 0, u32 name offset (NUL terminated, in the ".PAH")}; the offsets ascend and are 0x800 aligned. The ".PAC" next to it (438 MB) holds the data.
// The ".PAH" is the argument; the members (".PIL", ".PIM", ".IGB" ... Alchemy / Sting assets) are streamed out of the ".PAC" under their names.
static enumError extract_pahpac_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".pah") && !is_ext (arg, ".PAH"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	FILE *pac = 0;
	if (sz < 0x90 || rd_le32 (raw + 4) != 0x70)
		goto done;
	{
		const u32 n = rd_le32 (raw);
		if (n < 2 || n > 200000 || 0x70 + 16 * (u64)n > sz)
			goto done;
		char pacname[PATH_MAX];
		snprintf (pacname, sizeof (pacname), "%s", arg);
		size_t l = strlen (pacname);
		memcpy (pacname + l - 3, "PAC", 3);
		pac = fopen (pacname, "rb");
		if (!pac)
		{
			memcpy (pacname + l - 3, "pac", 3);
			pac = fopen (pacname, "rb");
		}
		if (!pac || fseeko (pac, 0, SEEK_END))
			goto done;
		const u64 psz = (u64)ftello (pac);
		u64 prev = 0;
		for (u32 i = 0; i < n; i++)
		{
			const u8 *e = raw + 0x70 + 16 * (size_t)i;
			const u64 o = rd_le32 (e), s = rd_le32 (e + 4), no = rd_le32 (e + 12);
			if (o < prev || o + s > psz || rd_le32 (e + 8) || no >= sz)
				goto done;
			uint k = 0;
			while (no + k < sz && raw[no + k])
			{
				if (raw[no + k] < 0x20 || raw[no + k] > 0x7e || raw[no + k] == '/' || raw[no + k] == '\\')
					goto done;
				k++;
			}
			if (!k || no + k >= sz)
				goto done;
			prev = o + s;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT PAH:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		u8 *buf = MALLOC (1 << 20);
		for (u32 i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = raw + 0x70 + 16 * (size_t)i;
			u64 o = rd_le32 (e), s = rd_le32 (e + 4);
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", (ccp)raw + rd_le32 (e + 12));
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (fseeko (pac, (off_t)o, SEEK_SET))
				err = ERR_READ_FAILED;
			while (!err && s)
			{
				const size_t chunk = s > (1 << 20) ? (1 << 20) : (size_t)s;
				if (fread (buf, 1, chunk, pac) != chunk)
					err = ERR_READ_FAILED;
				else if (F.f && fwrite (buf, 1, chunk, F.f) != chunk)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)chunk, path);
				s -= chunk;
			}
			ResetFile (&F, opt_preserve);
		}
		FREE (buf);
		if (!err && !testmode)
		{
			enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	if (pac)
		fclose (pac);
	FREE (raw);
	return err;
}
