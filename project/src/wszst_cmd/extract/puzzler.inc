// Puzzler Collection (Wii) data files.
//  ".txz" text packs: "ZZ" + locale name (e.g. "US_ENGLISH"), a 0x0a byte, u32 LE decompressed size, then one zlib stream holding the CRLF text script (`GAMESCRIPT_* "text"` lines). Written as "<file>.txt".
//  ".lpz" "PZPK" record packs, little-endian: {"PZPK", u32 0x10, u32 count, u32 index bytes (count * 16)}, then count 16-byte index entries {u32 index, 4 bytes, u32 data offset, 4 flag bytes}; the record data
//  follows the index (the first offset equals 0x10 + index bytes), each record runs to the next offset (the last to the end of the file). Records (puzzle / clue data) are written as "<file>.d/<index>.bin".
static enumError extract_puzzler_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	const bool txz = is_ext (arg, ".txz"), lpz = is_ext (arg, ".lpz");
	if (!txz && !lpz)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *txt = 0;
	if (txz)
	{
		size_t nl = 2;
		while (nl < 32 && nl < sz && raw[nl] != 0x0a)
			nl++;
		if (sz < 0x20 || memcmp (raw, "ZZ", 2) || nl >= 32 || nl + 7 > sz || raw[nl + 5] != 0x78)
			goto done;
		uint osz = 0;
		size_t used = 0;
		txt = fst_inflate (raw + nl + 5, sz - nl - 5, &osz, &used);
		if (!txt || osz != rd_le32 (raw + nl + 1))
			goto done;
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.txt", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT TXZ:%s -> %s\n", arg, dest);
			err = ERR_OK;
			goto done;
		}
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f && fwrite (txt, 1, osz, F.f) != osz)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", osz, dest);
		ResetFile (&F, opt_preserve);
		if (verbose >= 0 && !err)
			fprintf (stdlog, "%sEXTRACT TXZ:%s -> %s\n", verbose > 0 ? "\n" : "", arg, dest);
	}
	else
	{
		const u32 cnt = sz >= 0x20 ? rd_le32 (raw + 8) : 0, ib = sz >= 0x20 ? rd_le32 (raw + 12) : 0;
		if (sz < 0x20 || memcmp (raw, "PZPK", 4) || rd_le32 (raw + 4) != 0x10 || !cnt || cnt > 0x100000 || ib != 16 * cnt || 0x10 + (u64)ib > sz)
			goto done;
		u32 prev = 0x10 + ib;
		for (u32 i = 0; i < cnt; i++)
		{
			const u8 *e = raw + 0x10 + 16 * (size_t)i;
			const u32 o = rd_le32 (e + 8);
			if (rd_le32 (e) != i || o < prev || o > sz || (!i && o != 0x10 + ib))
				goto done;
			prev = o;
		}
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.d", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT PZPK:%s (%u records) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, cnt, dest);
		err = ERR_OK;
		for (u32 i = 0; i < cnt && !err && !testmode; i++)
		{
			const u32 o = rd_le32 (raw + 0x10 + 16 * (size_t)i + 8), e = i + 1 < cnt ? rd_le32 (raw + 0x10 + 16 * (size_t)i + 24) : (u32)sz;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%05u.bin", dest, basedir ? basedir : "", i);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && e > o && fwrite (raw + o, 1, e - o, F.f) != e - o)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e - o, path);
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (txt);
	FREE (raw);
	return err;
}
