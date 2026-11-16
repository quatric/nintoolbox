// Tenchu 4 (Wii) offset-table packs, big-endian, three layouts, members stored back to back (the first starts right behind the table, the last ends at the end of the file):
//   ".earc" (Common/Effect/*.earc): u32 count, count * {u32 offset, u32 size, u32 kind (4, 7, ...), u32 0}
//   ".ires" (Common/Model/**/*.ires): u32 count, count * {u32 offset, u32 size, u32 0}
//   ".ui"   (<LANG>/UI/*.ui): u32 count, u32 n1, u32 n2, u32 0, count * {u32 offset, u32 size, u32 0xffffffff}
// Members are written as "<file>.d/NNNN.<ext>" (ext from the magic: tpl, brres, arc, ... else bin) and extracted recursively.
static enumError extract_tenchupack_file (ccp arg, ccp basedir, uint depth)
{
	const bool earc = is_ext (arg, ".earc"), ires = is_ext (arg, ".ires"), ui = is_ext (arg, ".ui");
	if (!earc && !ires && !ui)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	const size_t es = earc ? 16 : 12, hs = ui ? 0x10 : 4;
	const u32 cnt = sz >= 0x20 ? rd_be32 (raw) : 0;
	if (sz < 0x20 || !cnt || cnt > 0x100000 || hs + es * (u64)cnt > sz)
		goto done;
	{
		u64 expect = hs + es * (u64)cnt;
		for (u32 i = 0; i < cnt; i++)
		{
			const u8 *e = raw + hs + es * (size_t)i;
			const u64 o = rd_be32 (e), s = rd_be32 (e + 4);
			const u32 f = rd_be32 (e + 8);
			if (o != expect || o + s > sz || (earc && (f > 0x20 || rd_be32 (e + 12))) || (ires && f) || (ui && f != 0xffffffff && f > 0xffff))
				goto done;
			expect = o + s;
		}
		if (expect + 0x20 < sz)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT TENCHU-PACK:%s (%u members) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, cnt, dest);
		err = ERR_OK;
		for (u32 i = 0; i < cnt && !err && !testmode; i++)
		{
			const u8 *e = raw + hs + es * (size_t)i;
			const u32 o = rd_be32 (e), s = rd_be32 (e + 4);
			ccp ext = "bin";
			if (s >= 4)
			{
				const u8 *m = raw + o;
				if (!memcmp (m, "\x00\x20\xaf\x30", 4))
					ext = "tpl";
				else if (!memcmp (m, "bres", 4))
					ext = "brres";
				else if (!memcmp (m, "U\xaa" "8-", 4))
					ext = "arc";
				else if (!memcmp (m, "RIFF", 4))
					ext = "wav";
			}
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%04u.%s", dest, basedir ? basedir : "", i, ext);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && s && fwrite (raw + o, 1, s, F.f) != s)
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
