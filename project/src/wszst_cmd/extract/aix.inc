// CRI "AIXF" containers (".aix": Sonic Unleashed, Metroid: Other M), big-endian: "AIXF", u32 header size - 8 @4,
// u32 0x01000014 @8, u32 0x800 @0xc, u16 segment count @0x18 and at 0x20 per segment {u32 offset, u32 size, s32 samples,
// s32 rate}; behind them a 0x10-byte sub-table (first byte 1) and the layer table with the layer count at +0x10.
// A segment is a run of "AIXP" chunks {id, u32 size - 8, s8 layer, u8 layer count, s16 data size, -1, data @0x10}
// interleaving the layers, each an ADX stream. Every layer of every segment is written as "sNN_lNN.adx" (decoded to
// WAV by the ADX extractor with xx); a file of one segment and one layer yields "audio.adx".
static enumError extract_aix_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".aix"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint segs = sz >= 0x20 ? rd_be16 (raw + 0x18) : 0;
	const size_t data0 = sz >= 8 ? (size_t)rd_be32 (raw + 4) + 8 : 0;
	const size_t sub = 0x20 + 0x10 * (size_t)segs, lay = sub + 0x10;
	if (sz < 0x800 || memcmp (raw, "AIXF", 4) || rd_be32 (raw + 8) != 0x01000014 || rd_be32 (raw + 0xc) != 0x800
		|| !segs || segs > 120 || lay + 8 >= data0 || data0 > sz || raw[sub] != 1 || !raw[lay] || raw[lay] > 16
		|| rd_be32 (raw + 0x20) != data0)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint layers = raw[lay];
	nintendo_sarc_entry_t *entries = CALLOC ((size_t)segs * layers, sizeof (*entries));
	uint n = 0;
	for (uint s = 0; entries && s < segs; s++)
	{
		const size_t so = rd_be32 (raw + 0x20 + 0x10 * (size_t)s);
		size_t ss = rd_be32 (raw + 0x24 + 0x10 * (size_t)s);
		if (so >= sz)
			continue;
		if (ss > sz - so)
			ss = sz - so;
		for (uint l = 0; l < layers; l++)
		{
			u8 *buf = MALLOC (ss ? ss : 1);
			size_t used = 0;
			for (size_t p = so; buf && p + 0x10 <= so + ss;)
			{
				const size_t bs = (size_t)rd_be32 (raw + p + 4) + 8;
				if (memcmp (raw + p, "AIXP", 4) || bs < 0x10 || p + bs > sz)
					break;
				const uint dsz = rd_be16 (raw + p + 0x0a);
				if (raw[p + 8] == l && dsz && 0x10 + (size_t)dsz <= bs)
				{
					memcpy (buf + used, raw + p + 0x10, dsz);
					used += dsz;
				}
				p += bs;
			}
			char name[32];
			if (segs == 1 && layers == 1)
				snprintf (name, sizeof (name), "audio.adx");
			else
				snprintf (name, sizeof (name), "s%02u_l%02u.adx", s, l);
			if (buf && used > 0x10 && OwnedEntryAdd (entries, n, name, buf, (uint)used))
				n++;
			FREE (buf);
		}
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (entries && n)
		err = write_owned_entries (arg, basedir, depth, "AIX", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
