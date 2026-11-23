// Military Madness: Nectaris ".sir" stream files (WiiWare), big-endian: a small table of {id, size, unpacked size of the previous stream, next id} rows (not interpreted) followed by "ZBLK" blocks {char "ZBLK", u32 block index,
// u32 stored size, u32 unpacked size (65,000 for a full block), one zlib stream}. The index restarts at 0 for every stream (four per file in the samples; their unpacked sizes 614,568 / 4,394,088 / 1,220 / 1,668 match the table
// offsets). Blocks follow each other, sometimes after padding up to the next 0x800 boundary, so each block is searched for after the end of the previous one and accepted only when its zlib stream inflates to exactly the stated size.
// Every stream is written as "<file>.d/<file>.<n>.bin" (level data: "anim cam" tables, floats; the contents are not decoded).
static enumError extract_sirzblk_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".sir"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *out = 0;
	size_t sz = 0, on = 0, cap = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enum { MAXS = 64 };
	nintendo_sarc_entry_t *entries = CALLOC (MAXS, sizeof (*entries));
	uint n = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	const char *b = strrchr (arg, '/');
	b = b ? b + 1 : arg;
	size_t p = 0;
	for (bool more = true; more;)
	{
		size_t cs = 0, us = 0;
		bool hit = false;
		uLongf dl = 0;
		u32 idx = 0;
		while (p + 16 <= sz && !hit)
		{
			if (memcmp (raw + p, "ZBLK", 4))
			{
				p += 4;
				continue;
			}
			idx = rd_be32 (raw + p + 4);
			cs = rd_be32 (raw + p + 8);
			us = rd_be32 (raw + p + 12);
			if (!us || us > 0x1000000 || cs > sz - p - 16 || on + us > 0x40000000)
			{
				p += 4;
				continue;
			}
			if (on + us > cap)
			{
				cap = (on + us) * 2;
				out = REALLOC (out, cap);
				if (!out)
				{
					err = ERR_OUT_OF_MEMORY;
					goto done;
				}
			}
			dl = us;
			if (idx == 0 && on)
				break; // new stream: flush the current one first, this block is decoded on the next round
			hit = uncompress (out + on, &dl, raw + p + 16, cs) == Z_OK && dl == us;
			if (!hit)
				p += 4;
		}
		more = hit || (idx == 0 && on && p + 16 <= sz);
		if (hit)
		{
			on += us;
			p = (p + 16 + cs + 3) & ~(size_t)3; // markers are 4-byte aligned
		}
		// flush at the end of the file or when the next block starts a new stream
		if (on && (!more || (!hit && idx == 0)))
		{
			char name[PATH_MAX];
			snprintf (name, sizeof (name), "%s.%u.bin", b, n);
			if (n >= MAXS || on > UINT_MAX || !OwnedEntryAdd (entries, n, name, out, (uint)on))
			{
				err = ERR_NOTHING_TO_DO;
				goto done;
			}
			n++;
			on = 0;
			if (p + 16 > sz)
				more = false;
		}
	}
	if (n)
	{
		err = write_owned_entries (arg, basedir, depth, "SIR", entries, n);
		entries = 0;
	}
done:
	if (entries)
		ResetOwnedEntries (entries, MAXS);
	FREE (out);
	FREE (raw);
	return err;
}
