// Home Button Menu data ".hbm" (nativeWii/system/homebutton/*.hbm, e.g. Sengoku Basara), big-endian: {"\0MBH", u32 version (4), u32 offset[5], ...}; the offsets point at back-to-back sections (resource
// U8 archives, a UTF-16 message table, a text index, a second U8 archive); the last section runs to the end of the file. Sections are written as "<file>.d/NN.<ext>" (arc, txt for UTF-16 BOM text, else bin) and
// extracted recursively.
static enumError extract_hbmpack_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".hbm"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 || memcmp (raw, "\0MBH", 4) || rd_be32 (raw + 4) != 4)
		goto done;
	{
		const uint cnt = 5;
		u32 off[6];
		for (uint i = 0; i < cnt; i++)
			off[i] = rd_be32 (raw + 8 + 4 * i);
		off[cnt] = sz;
		if (off[0] < 0x20)
			goto done;
		for (uint i = 0; i < cnt; i++)
			if (off[i] >= off[i + 1] || off[i + 1] > sz)
				goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT HBM:%s (%u sections) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, cnt, dest);
		err = ERR_OK;
		for (uint i = 0; i < cnt && !err && !testmode; i++)
		{
			const u8 *m = raw + off[i];
			const u32 s = off[i + 1] - off[i];
			ccp ext = "bin";
			if (s >= 4 && !memcmp (m, "U\xaa" "8-", 4))
				ext = "arc";
			else if (s >= 2 && m[0] == 0xfe && m[1] == 0xff)
				ext = "txt";
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%02u.%s", dest, basedir ? basedir : "", i, ext);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (m, 1, s, F.f) != s)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
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
