// Stormfront ".seg" sound (Spiderwick Chronicles, Eragon; Wii variant): "seg\0", codec "wii\0" @4, version be32 @8,
// size @0x10, rate @0x18, samples @0x1c, loop flag @0x20, channels @0x24, name @0x38; the audio starts at 0x4000 as
// 0x2000-byte blocks interleaved per channel, and the first block of every channel opens with its own standard
// 0x60-byte DSP header. Each channel becomes a standalone "NN.dsp" (the nibble count of the header covers the
// channel's data, so the stream is cut to that).
static enumError extract_seg_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".seg"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint ch = sz > 0x4060 ? rd_be32 (raw + 0x24) : 0;
	if (sz < 0x4060 || memcmp (raw, "seg\0wii\0", 8) || ch < 1 || ch > 2 || 0x4000 + 0x2000 * (size_t)ch > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (ch, sizeof (*entries));
	bool good = entries != 0;
	for (uint c = 0; c < ch && good; c++)
	{
		const u8 *h = raw + 0x4000 + 0x2000 * (size_t)c;
		const size_t n = ((size_t)rd_be32 (h + 4) + 1) / 2;
		u8 *out = n && n < sz ? CALLOC (1, 0x60 + n) : 0;
		if (!out || !rd_be32 (h + 8))
		{
			FREE (out);
			good = false;
			break;
		}
		memcpy (out, h, 0x60);
		size_t got = 0;
		for (size_t r = 0; got < n; r++)
		{
			const size_t skip = r ? 0 : 0x60, base = 0x4000 + (r * ch + c) * 0x2000 + skip;
			if (base >= sz)
				break;
			size_t len = 0x2000 - skip;
			if (len > n - got)
				len = n - got;
			if (len > sz - base)
				len = sz - base;
			memcpy (out + 0x60 + got, raw + base, len);
			got += len;
		}
		char name[16];
		snprintf (name, sizeof (name), "%02u.dsp", c);
		good = got == n && OwnedEntryAdd (entries, c, name, out, 0x60 + n);
		FREE (out);
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (good)
		err = write_owned_entries (arg, basedir, depth, "SEG", entries, ch);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
