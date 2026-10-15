// Sumo Digital ".abc" audio libraries (Sonic & SEGA All-Stars Racing, Wii): complete DSP-ADPCM files back to back,
// each (0x60-byte header + (nibbles + 1) / 2 bytes of data) followed by 1..0x100 padding bytes up to the next multiple
// of 0x100. Every stream is written as "NNNN.dsp".
static enumError extract_abc_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".abc"))
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
	while (o < sz)
	{
		if (o + 0x60 > sz)
			break;
		const u32 ns = rd_be32 (raw + o), nib = rd_be32 (raw + o + 4), rate = rd_be32 (raw + o + 8);
		if (!nib || rate < 4000 || rate > 96000 || (u64)ns + 14 < (u64)nib * 14 / 16 || (u64)ns > (u64)nib * 14 / 16 + 14
			|| rd_be32 (raw + o + 0x10) != 2 || rd_be16 (raw + o + 0x0e))
			break;
		const size_t n = 0x60 + (nib + 1) / 2;
		if (n > sz - o)
			break;
		o = (o + n + 0x100) & ~(size_t)0xff;
		count++;
	}
	nintendo_sarc_entry_t *entries = count && o == sz ? CALLOC (count, sizeof (*entries)) : 0;
	bool good = entries != 0;
	o = 0;
	for (uint i = 0; i < count && good; i++)
	{
		const size_t n = 0x60 + (rd_be32 (raw + o + 4) + 1) / 2;
		char name[24];
		snprintf (name, sizeof (name), "%04u.dsp", i);
		good = OwnedEntryAdd (entries, i, name, raw + o, n);
		o = (o + n + 0x100) & ~(size_t)0xff;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (good)
		err = write_owned_entries (arg, basedir, depth, "ABC", entries, count);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
