// Capcom MT Framework Wii sound packs (type hash 0x7e33a16c, magic "CAPS"): big-endian
// {"CAPS", u32 version (11), u32 count, u32 1, u32 count, u32 cue table, u32 cue data,
// u32 wave data offset}, then count 0x20-byte "DSPW" descriptors from 0x20 (+0x08 = size of
// descriptor + wave block, +0x18 channels, +0x1c samples) and the wave blocks (DSP headers +
// ADPCM per channel) packed back to back from the wave data offset. Each sound is rebuilt
// as a standalone "NNNN.dspw" (descriptor + wave block) and then split by the DSPW extractor.
static enumError extract_capcaps_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 0x40 ? rd_be32 (raw + 8) : 0;
	const size_t base = sz >= 0x40 ? rd_be32 (raw + 0x1c) : 0;
	if (sz < 0x40 || memcmp (raw, "CAPS", 4) || !count || count > 4096 || 0x20 + 0x20 * (size_t)count > sz
		|| base > sz || base < 0x20 + 0x20 * (size_t)count)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (count, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	size_t pos = base;
	uint n = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *d = raw + 0x20 + 0x20 * (size_t)i;
		const size_t S = rd_be32 (d + 8);
		if (memcmp (d, "DSPW", 4) || S < 0x80 || S - 0x20 > sz - pos)
			break;
		u8 *buf = MALLOC (S);
		if (!buf)
			break;
		memcpy (buf, d, 0x20);
		memcpy (buf + 0x20, raw + pos, S - 0x20);
		pos += S - 0x20;
		char name[32];
		snprintf (name, sizeof (name), "%04u.dspw", i);
		const bool ok = OwnedEntryAdd (entries, n, name, buf, S);
		FREE (buf);
		if (!ok)
			break;
		n++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (n)
		err = write_owned_entries (arg, basedir, depth, "CAPS", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
