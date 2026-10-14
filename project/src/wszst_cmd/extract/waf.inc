// Major Minor's Majestic March ".waf" (Wii) sound stream, big-endian: a chain of chunks {u16 1, u16 2, u32 size, u32 samples, u32 rate,
// then a complete 0x60-byte DSP-ADPCM header and its data (size bytes, the header's nibble count / 2 + 0x60)},
// each chunk starting at the next multiple of 0x20 after the previous one's end (offset + 0x10 + size). Every chunk
// carries its own coefficients and history, so each becomes a standalone "NNNN.dsp".
static enumError extract_waf_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".waf"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	uint count = 0;
	size_t o = 0;
	for (; o + 0x70 <= sz && rd_be32 (raw + o) == 0x00010002; count++)
	{
		const size_t n = rd_be32 (raw + o + 4);
		if (n < 0x60 || n > sz - o - 0x10 || (size_t)(rd_be32 (raw + o + 0x14) + 1) / 2 + 0x60 != n)
			break;
		o = (o + 0x10 + n + 0x1f) & ~(size_t)0x1f;
	}
	nintendo_sarc_entry_t *entries = count && o == sz ? CALLOC (count, sizeof (*entries)) : 0;
	bool good = entries != 0;
	o = 0;
	for (uint i = 0; i < count && good; i++)
	{
		const size_t n = rd_be32 (raw + o + 4);
		char name[24];
		snprintf (name, sizeof (name), "%04u.dsp", i);
		good = OwnedEntryAdd (entries, i, name, raw + o + 0x10, n);
		o = (o + 0x10 + n + 0x1f) & ~(size_t)0x1f;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (good)
		err = write_owned_entries (arg, basedir, depth, "WAF", entries, count);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
