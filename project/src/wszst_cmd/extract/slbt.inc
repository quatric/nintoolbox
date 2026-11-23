// "SLBT" (".atb", animation/sprite banks of the Battle of Giants engine, about 80 DS games): see slbt_unpack for the layout (raw, or one Nintendo LZ10 stream). The unpacked data is tile pixels, written as "<file>.d/<name>.bin".
// The bit depth is not stored in the file (the palette in the sibling data decides), so the data is also written as a 16 tiles wide tile sheet for both readings, "<name>.4bpp.ncgr" and "<name>.8bpp.ncgr"
// (tiles padded with empty ones), which the rest of the tool converts to PNG with the default palette; pick the one that looks right. A 4 bit reading of the Battle of Giants "attachement_gold.atb" gives clean sprites.
// Reuses packlz_lz10 from packlz.inc. The "DTBT" variant of the same engine is left alone.
static u8 *slbt_ncgr (const u8 *d, u32 n, uint bpp, u32 *tot)
{
	const u32 ts = bpp * 8, rows = (n / ts + 15) / 16, pn = rows * 16 * ts;
	u8 *out = CALLOC (1, 0x30 + pn);
	if (!out)
		return 0;
	memcpy (out, "RGCN", 4);
	wr_le16 (out + 4, 0xfeff);
	wr_le16 (out + 6, 0x0101);
	wr_le32 (out + 8, 0x30 + pn);
	wr_le16 (out + 12, 0x10);
	wr_le16 (out + 14, 1);
	memcpy (out + 16, "RAHC", 4);
	wr_le32 (out + 20, pn + 0x20);
	wr_le16 (out + 24, (u16)rows);
	wr_le16 (out + 26, 16);
	wr_le32 (out + 28, bpp == 4 ? 3 : 4);
	wr_le32 (out + 40, pn);
	wr_le32 (out + 44, 0x18);
	memcpy (out + 48, d, n);
	*tot = 0x30 + pn;
	return out;
}

// Unpacks an SLBT file: u32 unpacked size at 4, u8 flag at 8 (0 raw, 1 Nintendo LZ10), u16 chunk count at 9, count * u32 chunk sizes (size << 8) from 0x0c, one pad byte; a raw file then
// carries the data, an LZ10 one carries {u32 payload size, LZ10 stream} that must unpack to exactly the stored size. Returns a malloc block in *out.
static bool slbt_unpack (const u8 *raw, size_t sz, u8 **out, size_t *outn, bool *is_raw)
{
	if (sz < 0x16 || memcmp (raw, "SLBT", 4) || raw[8] > 1)
		return false;
	const size_t hl = 0x0d + 4 * (size_t)rd_le16 (raw + 9), tot = rd_le32 (raw + 4);
	*is_raw = !raw[8];
	if (hl >= sz || !tot)
		return false;
	if (!raw[8])
	{
		if (sz - hl != tot)
			return false;
		*out = MALLOC (tot);
		if (!*out)
			return false;
		memcpy (*out, raw + hl, tot);
		*outn = tot;
		return true;
	}
	size_t used;
	if (hl + 4 >= sz || rd_le32 (raw + hl) != sz - hl - 4 || !packlz_lz10 (raw + hl + 4, sz - hl - 4, &used, out, outn))
		return false;
	if (*outn != tot || used != sz - hl - 4)
	{
		FREE (*out);
		*out = 0;
		return false;
	}
	return true;
}

static enumError extract_slbt_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0, *out = 0;
	size_t sz = 0, n;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		goto done;
	bool is_raw;
	if (!slbt_unpack (raw, sz, &out, &n, &is_raw))
		goto done;
	nintendo_sarc_entry_t *entries = CALLOC (3, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	const char *s = strrchr (arg, '/');
	char name[PATH_MAX];
	snprintf (name, sizeof (name), "%s", s ? s + 1 : arg);
	const size_t l = strlen (name);
	uint k = 0;
	snprintf (name + l, sizeof (name) - l, ".bin");
	if (OwnedEntryAdd (entries, k, name, out, (uint)n))
		k++;
	for (uint bpp = 4; bpp <= 8; bpp += 4)
	{
		u32 tot;
		u8 *nc = slbt_ncgr (out, (uint)n, bpp, &tot);
		snprintf (name + l, sizeof (name) - l, ".%ubpp.ncgr", bpp);
		if (nc && OwnedEntryAdd (entries, k, name, nc, tot))
			k++;
		FREE (nc);
	}
	if (k)
		err = write_owned_entries (arg, basedir, depth, "SLBT", entries, k);
	else
		ResetOwnedEntries (entries, 3);
done:
	FREE (out);
	FREE (raw);
	return err;
}
