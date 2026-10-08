// "NDP\0" stereo DSP music (Pool Hall Pro ".nds"; Storm City Games): little-endian
//   +0x00 "NDP\0", +0x04 version (3), +0x08 u32 (size - 0x18), +0x0c sample rate,
//   +0x10 u32 channels, +0x14 u32 0x10, +0x18 one standard 0x60-byte big-endian
//   GameCube DSP header per channel; the ADPCM data follows with the channels
//   interleaved in 4-byte units. Each channel is written as a ".dsp" file.
static enumError extract_ndp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".nds"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint ch = raw_size > 0x20 ? rd_le32 (raw + 0x10) : 0;
	if (raw_size < 0x100 || memcmp (raw, "NDP\0", 4) || ch < 1 || ch > 8 || 0x18 + 0x60 * (size_t)ch > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 nibbles = rd_be32 (raw + 0x18 + 4);
	const size_t per = (((size_t)nibbles + 15) / 16) * 8;
	const size_t total = per * ch;
	const size_t hdr_end = 0x18 + 0x60 * (size_t)ch;
	if (!per || total > raw_size - hdr_end)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t start = raw_size - total;
	if (start < hdr_end || start > hdr_end + 0x20)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (ch, sizeof (*entries));
	u8 *tmp = MALLOC (0x60 + per);
	if (!entries || !tmp)
	{
		FREE (entries);
		FREE (tmp);
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	for (uint c = 0; c < ch; c++)
	{
		memcpy (tmp, raw + 0x18 + 0x60 * c, 0x60);
		u8 *out = tmp + 0x60;
		for (size_t o = 0; o < per; o += 4)
		{
			const size_t src = start + (o / 4) * 4 * ch + 4 * c;
			const size_t take = per - o < 4 ? per - o : 4;
			if (src + take > raw_size)
				break;
			memcpy (out + o, raw + src, take);
		}
		char name[32];
		snprintf (name, sizeof (name), "ch%u.dsp", c);
		if (!OwnedEntryAdd (entries, n, name, tmp, 0x60 + per))
			break;
		n++;
	}
	FREE (tmp);
	enumError err = ERR_NOTHING_TO_DO;
	if (n)
		err = write_owned_entries (arg, basedir, depth, "NDP", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
