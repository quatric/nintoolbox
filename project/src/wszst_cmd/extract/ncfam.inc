// "NCCL" / "NCCG" / "NCSC" / "NCOB" files (".ncl" / ".ncg" / ".nsc" of about 100 DS games, for example 10 Second Run, Prinzessin Lillifee, A Ressha de Ikou DS): the intermediate NITRO-CHARACTER source files from which NCLR/NCGR/NSCR are built. Little endian: {char magic[4], u16 0xfffe, u16 version, u32 size, u16 0x10, u16 nblocks}, then blocks {char tag[4], u32 size incl. 8 byte header, ...}. "PALT" {u32 a, u32 b, a*b u16 BGR555 colors}, "CHAR" {u32 tiles wide, u32 tiles high, tile data (4 or 8 bit, from the size), ATTR block}, "SCRN" {u32 tiles wide, u32 tiles high, u32, u32, u16 map entries}. Written as the standard "<name>.nclr" / ".ncgr" / ".nscr" (without their ancillary blocks), which the rest of the tool already converts.
// "NCOB" cell files (".nce"): same block layout; only the embedded "CHAR" block {u32 1, u32 size, 4 bit tiles} is exported, as "<name>.ncgr" 16 tiles wide (padded with empty tiles). The CELL/ANIM blocks are not decoded.
static enumError extract_ncob_chars (ccp arg, ccp basedir, uint depth, const u8 *raw, size_t sz)
{
	if (sz < 0x30 || rd_le16 (raw + 4) != 0xfeff)
		return ERR_NOTHING_TO_DO;
	size_t o = 16;
	while (o + 16 <= sz && memcmp (raw + o, "CHAR", 4))
	{
		const u32 s = rd_le32 (raw + o + 4);
		if (s < 8 || s > sz - o)
			return ERR_NOTHING_TO_DO;
		o += s;
	}
	if (o + 16 > sz)
		return ERR_NOTHING_TO_DO;
	const u32 n = rd_le32 (raw + o + 12);
	if (!n || n % 32 || rd_le32 (raw + o + 4) != n + 16 || o + 16 + n > sz)
		return ERR_NOTHING_TO_DO;
	const u32 rows = (n / 32 + 15) / 16, pn = rows * 16 * 32, tot = 0x30 + pn;
	u8 *out = CALLOC (1, tot);
	nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
	enumError err = ERR_OUT_OF_MEMORY;
	if (out && entries)
	{
		memcpy (out, "RGCN", 4);
		wr_le16 (out + 4, 0xfeff);
		wr_le16 (out + 6, 0x0101);
		wr_le32 (out + 8, tot);
		wr_le16 (out + 12, 0x10);
		wr_le16 (out + 14, 1);
		memcpy (out + 16, "RAHC", 4);
		wr_le32 (out + 20, pn + 0x20);
		wr_le16 (out + 24, (u16)rows);
		wr_le16 (out + 26, 16);
		wr_le32 (out + 28, 3);
		wr_le32 (out + 40, pn);
		wr_le32 (out + 44, 0x18);
		memcpy (out + 48, raw + o + 16, n);
		const char *p = strrchr (arg, '/');
		char name[PATH_MAX];
		snprintf (name, sizeof (name), "%s", p ? p + 1 : arg);
		char *e = strrchr (name, '.');
		if (e)
			*e = 0;
		const size_t l = strlen (name);
		snprintf (name + l, sizeof (name) - l, ".ncgr");
		err = ERR_NOTHING_TO_DO;
		if (OwnedEntryAdd (entries, 0, name, out, tot))
		{
			out = 0;
			err = write_owned_entries (arg, basedir, depth, "NCOB", entries, 1);
			entries = 0;
		}
	}
	if (entries)
		ResetOwnedEntries (entries, 1);
	FREE (out);
	return err;
}

static enumError extract_ncfam_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0, *out = 0;
	size_t sz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	nintendo_sarc_entry_t *entries = 0;
	{
		u8 magic[4];
		FILE *f = fopen (arg, "rb");
		const bool hit = f && fread (magic, 1, 4, f) == 4
			&& (!memcmp (magic, "NCCL", 4) || !memcmp (magic, "NCCG", 4) || !memcmp (magic, "NCSC", 4) || !memcmp (magic, "NCOB", 4));
		if (f)
			fclose (f);
		if (!hit)
			return ERR_NOTHING_TO_DO;
	}
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		goto done;
	if (sz > 0x4000000 && !memcmp (raw, "NCOB", 4))
		goto done;
	if (!memcmp (raw, "NCOB", 4))
	{
		err = extract_ncob_chars (arg, basedir, depth, raw, sz);
		goto done;
	}
	if (sz < 0x30 || sz > 0x4000000 || rd_le16 (raw + 4) != 0xfeff || !memcmp (raw + 16, "CMNT", 4))
		goto done;
	const char *tag = !memcmp (raw, "NCCL", 4) ? "PALT" : !memcmp (raw, "NCCG", 4) ? "CHAR" : "SCRN";
	if (memcmp (raw + 16, tag, 4))
		goto done;
	const u32 bsz = rd_le32 (raw + 20), a = rd_le32 (raw + 24), b = rd_le32 (raw + 28);
	if (bsz < 0x14 || bsz > sz - 16)
		goto done;
	const u8 *src;
	u32 n, hdr;
	const char *ext;
	if (*tag == 'P')
	{
		n = a * b * 2;
		hdr = 0x10;
		ext = ".nclr";
		if (!n || bsz != n + 0x10 || a > 256 || b > 256)
			goto done;
	}
	else if (*tag == 'C')
	{
		hdr = 0x10;
		n = bsz - 0x14;
		ext = ".ncgr";
		if (!a || !b || a > 0x1000 || b > 0x1000 || (n != a * b * 32 && n != a * b * 64))
			goto done;
	}
	else
	{
		hdr = 0x18;
		n = a * b * 2;
		ext = ".nscr";
		if (!a || !b || a > 0x100 || b > 0x100 || bsz != n + 0x18)
			goto done;
	}
	src = raw + 16 + hdr;
	if (16 + hdr + n > sz)
		goto done;
	const uint tot = *tag == 'P' ? 0x10 + 0x18 + n : *tag == 'C' ? 0x10 + 0x20 + n : 0x10 + 0x14 + n;
	out = CALLOC (1, tot);
	if (!out)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	wr_le16 (out + 4, 0xfeff);
	wr_le16 (out + 6, *tag == 'C' ? 0x0101 : 0x0100);
	wr_le32 (out + 8, tot);
	wr_le16 (out + 12, 0x10);
	wr_le16 (out + 14, 1);
	if (*tag == 'P')
	{
		memcpy (out, "RLCN", 4);
		memcpy (out + 16, "TTLP", 4);
		wr_le32 (out + 20, n + 0x18);
		wr_le32 (out + 24, a * b <= 16 ? 3 : 4);
		wr_le32 (out + 32, n);
		wr_le32 (out + 36, 0x10);
		memcpy (out + 40, src, n);
	}
	else if (*tag == 'C')
	{
		memcpy (out, "RGCN", 4);
		memcpy (out + 16, "RAHC", 4);
		wr_le32 (out + 20, n + 0x20);
		wr_le16 (out + 24, (u16)b);
		wr_le16 (out + 26, (u16)a);
		wr_le32 (out + 28, n == a * b * 32 ? 3 : 4);
		wr_le32 (out + 40, n);
		wr_le32 (out + 44, 0x18);
		memcpy (out + 48, src, n);
	}
	else
	{
		memcpy (out, "RCSN", 4);
		memcpy (out + 16, "NRCS", 4);
		wr_le32 (out + 20, n + 0x14);
		wr_le16 (out + 24, (u16)(a * 8));
		wr_le16 (out + 26, (u16)(b * 8));
		wr_le32 (out + 32, n);
		memcpy (out + 36, src, n);
	}
	entries = CALLOC (1, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	{
		const char *p = strrchr (arg, '/');
		char name[PATH_MAX];
		snprintf (name, sizeof (name), "%s", p ? p + 1 : arg);
		char *e = strrchr (name, '.');
		if (e)
			*e = 0;
		const size_t l = strlen (name);
		snprintf (name + l, sizeof (name) - l, "%s", ext);
		if (OwnedEntryAdd (entries, 0, name, out, tot))
		{
			out = 0;
			err = write_owned_entries (arg, basedir, depth, "NCFAM", entries, 1);
			entries = 0;
		}
	}
done:
	if (entries)
		ResetOwnedEntries (entries, 1);
	FREE (out);
	FREE (raw);
	return err;
}
