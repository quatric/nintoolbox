// ".zzz" chunk streams (X-Men: Destiny, Wii), big-endian: a plain concatenation of chunks
//   {u8 magic[8] = F0 0D CA FE 12 34 43 21, u32 payload size, u32 class hash, 36 more header bytes, payload}
// i.e. a 52 byte header followed by "payload size" bytes, the next chunk starting right behind it, and the last one
// ending exactly at the end of the file. The class hashes repeat heavily (shader / material parameter blocks,
// texture and vertex data); the header words behind the class hash and the payloads are not decoded.
// Every chunk is written whole (header included, so the files can be concatenated back to the original) as
// "<index>.<class hash>.bin" and the members are not recursed into. Two classes carry a known file and are written as the payload
// only: 544872c7 = DDS texture or Scaleform GFX movie ("<index>.544872c7.dds" / ".gfx", when it starts with "DDS " / "GFX") and 0e819532 = Wwise sound bank
// ("<index>.0e819532.bnk", when it starts with "BKHD"); run "wszst xx" on the folder to unpack those further.
static enumError extract_zzz_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".zzz"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	static const u8 magic[8] = { 0xf0, 0x0d, 0xca, 0xfe, 0x12, 0x34, 0x43, 0x21 };
	enum { ZZZ_HDR = 52 };
	uint n = 0;
	bool good = sz >= ZZZ_HDR;
	for (u64 p = 0; good && p < sz; n++)
	{
		if (p + ZZZ_HDR > sz || memcmp (raw + p, magic, sizeof (magic)))
		{
			good = false;
			break;
		}
		p += ZZZ_HDR + (u64)rd_be32 (raw + p + 8);
		if (p > sz)
			good = false;
	}
	nintendo_sarc_entry_t *entries = 0;
	uint cnt = 0;
	if (good && n)
	{
		entries = CALLOC ((size_t)n + 1, sizeof (*entries));
		good = entries != 0;
	}
	else
		good = false;
	for (u64 p = 0; good && p < sz && cnt < n;)
	{
		const u32 len = ZZZ_HDR + rd_be32 (raw + p + 8), cls = rd_be32 (raw + p + 12);
		ccp ext = "bin";
		u32 skip = 0; // payload only for the classes whose payload is a known file
		if (cls == 0x544872c7 && len >= ZZZ_HDR + 4 && !memcmp (raw + p + ZZZ_HDR, "DDS ", 4))
			ext = "dds", skip = ZZZ_HDR;
		else if (cls == 0x544872c7 && len >= ZZZ_HDR + 4 && !memcmp (raw + p + ZZZ_HDR, "GFX", 3))
			ext = "gfx", skip = ZZZ_HDR;
		else if (cls == 0x0e819532 && len >= ZZZ_HDR + 4 && !memcmp (raw + p + ZZZ_HDR, "BKHD", 4))
			ext = "bnk", skip = ZZZ_HDR;
		char name[0x40];
		snprintf (name, sizeof (name), "%04u.%08x.%s", cnt, cls, ext);
		good = OwnedEntryAdd (entries, cnt, name, raw + p + skip, len - skip);
		if (good)
			cnt++;
		p += len;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (good && cnt)
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT ZZZ:%s (%u chunks) -> %s/\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, cnt, dest);
		err = ERR_OK;
		for (uint i = 0; !err && !testmode && i < cnt; i++)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", entries[i].name);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && entries[i].size && fwrite (entries[i].data, 1, entries[i].size, F.f) != entries[i].size)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", entries[i].size, path);
			ResetFile (&F, opt_preserve);
		}
		ResetOwnedEntries (entries, cnt);
	}
	else
		FREE (entries);
	FREE (raw);
	return err;
}
