// Z-Axis "ZSND" sound banks, GCUB (GameCube/Wii) variant (Marvel: Ultimate Alliance 2; ".zss"/".zsm"),
// big-endian: "ZSND", "GCUB", u32 size, u32 header size, then config tables {u32 entries, u32 head offset, u32 body
// offset}; table 2 @0x1c/0x24 (0x18-byte entries: u16 layers @2 = 0/1 mono, 2 stereo, 0x22 four channels; u32 rate
// @4) and table 3 @0x28/0x30 (0x0c-byte entries {u32 stream offset, u32 stream size, "DSP "}). A stream starts with
// one 0x60-byte DSP header per channel followed by the data interleaved in 0x8000-byte blocks (the short last block
// is cut at the stream end, the missing tail reads as zero). Every channel becomes "NNNN[_chN].dsp" (layout after vgmstream).
static enumError extract_zsnd_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".zss") && !is_ext (arg, ".zsm"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x40 || memcmp (raw, "ZSNDGCUB", 8))
		goto skip;
	const size_t n2 = rd_be32 (raw + 0x1c), b2 = rd_be32 (raw + 0x24), n3 = rd_be32 (raw + 0x28), b3 = rd_be32 (raw + 0x30);
	if (!n3 || n3 > 100000 || n2 < n3 || b2 + 0x18 * n3 > sz || b3 + 0x0c * n3 > sz)
		goto skip;
	nintendo_sarc_entry_t *entries = CALLOC (n3 * 4, sizeof (*entries));
	uint n = 0;
	bool good = entries != 0;
	for (size_t s = 0; s < n3 && good; s++)
	{
		const u8 *e2 = raw + b2 + 0x18 * s, *e3 = raw + b3 + 0x0c * s;
		const uint layers = rd_be16 (e2 + 2), rate = rd_be32 (e2 + 4);
		const uint ch = layers == 2 ? 2 : layers == 0x22 ? 4 : 1;
		const size_t so = rd_be32 (e3), ss = rd_be32 (e3 + 4);
		if (layers > 2 && layers != 0x22)
			continue;
		if (!rate || ss <= 0x60 * ch || so > sz || ss > sz - so)
			continue;
		const size_t total = ss - 0x60 * ch, per = total / ch / 8 * 8, il = 0x8000;
		const size_t ns = per / 8 * 14;
		if (!per)
			continue;
		const u8 *body = raw + so + 0x60 * ch;
		for (uint c = 0; c < ch && good; c++)
		{
			u8 *out = CALLOC (1, 0x60 + per);
			if (!out)
			{
				good = false;
				break;
			}
			memcpy (out, raw + so + 0x60 * c, 0x60);
			for (size_t i = 0; i * il < per; i++)
			{
				const size_t src = (i * ch + c) * il, len = il < per - i * il ? il : per - i * il;
				if (src < total)
					memcpy (out + 0x60 + i * il, body + src, len < total - src ? len : total - src);
			}
			wr_be32 (out, (u32)ns);
			wr_be32 (out + 4, (u32)(per * 2));
			wr_be32 (out + 8, rate);
			wr_be32 (out + 0x14, (u32)(per * 2 - 1));
			char name[24];
			if (ch == 1)
				snprintf (name, sizeof (name), "%04u.dsp", (uint)s);
			else
				snprintf (name, sizeof (name), "%04u_ch%u.dsp", (uint)s, c);
			good = OwnedEntryAdd (entries, n, name, out, 0x60 + per);
			FREE (out);
			if (good)
				n++;
		}
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (good && n)
		err = write_owned_entries (arg, basedir, depth, "ZSND", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
skip:
	FREE (raw);
	return ERR_NOTHING_TO_DO;
}
