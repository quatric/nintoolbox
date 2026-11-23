// "NSC\0" (".nsc" / "_nsc.bin" tile maps of about 20 DS games, e.g. Boku to Sim no Machi Camera "title_logo_b_nsc.bin"): "NSC", 0x00, u16 0x0300, u16 0x0100, u8 width and u8 height in tiles (0x20 x 0x18), 2 zero bytes, then width * height standard DS screen entries (u16: tile 0-9, flip 10-11, palette 12-15); the file size is exactly 12 + 2 * width * height (checked on the 3 samples with the magic). Converted to a standard "RCSN" (NSCR) file "<file>.d/<name>.nscr", which the rest of the tool already turns into PNG next to a tile and palette file.
static enumError extract_nscmap_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0, *out = 0;
	size_t sz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	nintendo_sarc_entry_t *entries = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		goto done;
	if (sz < 16 || memcmp (raw, "NSC\0", 4) || rd_le16 (raw + 4) != 0x0300 || rd_le16 (raw + 6) != 0x0100)
		goto done;
	const uint w = raw[8], h = raw[9], n = 2 * w * h;
	if (!w || !h || sz != 12 + (size_t)n)
		goto done;
	const uint tot = 0x24 + n;
	out = CALLOC (1, tot);
	entries = CALLOC (1, sizeof (*entries));
	if (!out || !entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	memcpy (out, "RCSN", 4);
	wr_le16 (out + 4, 0xfeff);
	wr_le16 (out + 6, 0x0100);
	wr_le32 (out + 8, tot);
	wr_le16 (out + 12, 0x10);
	wr_le16 (out + 14, 1);
	memcpy (out + 16, "NRCS", 4);
	wr_le32 (out + 20, 0x14 + n);
	wr_le16 (out + 24, (u16)(w * 8));
	wr_le16 (out + 26, (u16)(h * 8));
	wr_le32 (out + 32, n);
	memcpy (out + 36, raw + 12, n);
	char name[PATH_MAX];
	const char *sl = strrchr (arg, '/');
	snprintf (name, sizeof (name), "%s.nscr", sl ? sl + 1 : arg);
	if (OwnedEntryAdd (entries, 0, name, out, tot))
		err = write_owned_entries (arg, basedir, depth, "NSC", entries, 1);
	else
		ResetOwnedEntries (entries, 1);
done:
	FREE (out);
	FREE (raw);
	return err;
}
