// Knight Wars / Aragorn's Quest ".kwa" (Wii), big-endian: u32 3, u32 channels @4, u32 interleave @0xc (0x8000); from
// 0x20 the channels' blocks of "interleave" bytes alternate, the first block of every channel opening with its own
// standard 0x60-byte DSP header (layout after vgmstream). Each channel becomes a standalone "NN.dsp".
static enumError extract_kwa_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".kwa"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint ch = sz > 0x80 ? rd_be32 (raw + 4) : 0;
	const size_t il = sz > 0x80 ? rd_be32 (raw + 0xc) : 0;
	if (sz < 0x80 || rd_be32 (raw) != 3 || ch < 1 || ch > 4 || il < 0x80 || il > 0x100000 || 0x20 + il * (size_t)(ch - 1) + 0x60 > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (ch, sizeof (*entries));
	bool good = entries != 0;
	for (uint c = 0; c < ch && good; c++)
	{
		const u8 *h = raw + 0x20 + il * (size_t)c;
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
			const size_t skip = r ? 0 : 0x60, base = 0x20 + (r * ch + c) * il + skip;
			if (base >= sz)
				break;
			size_t len = il - skip;
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
		err = write_owned_entries (arg, basedir, depth, "KWA", entries, ch);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
