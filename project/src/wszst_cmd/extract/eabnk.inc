// EA "BNKb" sound banks (big-endian BNK, GameCube/Wii; Tiger Woods PGA Tour
// "abnk_*.bin" hole-archive members): +0x00 "BNKb", +0x04 u8 version (4/5),
// +0x06 u16 count, +0x14 count * u32 offsets relative to their own slot (0 = unused).
// Each entry is an EA "PT" tag header (platform u16 LE at +2, then {tag, len, value}
// patches: 0xa0 codec 0x12 = GC DSP, 0x84 rate, 0x85 samples, 0x88 absolute data
// offset, 0x8f 33 bytes with the 16 DSP coefficients). Each sound is written as
// "NNNN.dsp".
static enumError extract_eabnk_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz > 0x20 ? (raw[6] << 8) | raw[7] : 0;
	if (sz < 0x20 || memcmp (raw, "BNKb", 4) || raw[4] < 4 || raw[4] > 5 || !count || 0x14 + 4 * (size_t)count > sz)
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
	uint n = 0;
	for (uint i = 0; i < count; i++)
	{
		const size_t slot = 0x14 + 4 * (size_t)i;
		const u32 rel = rd_be32 (raw + slot);
		if (!rel || rel >= sz - slot || sz - slot - rel < 8)
			continue;
		size_t p = slot + rel;
		if (raw[p] != 'P' || raw[p + 1] != 'T')
			continue;
		p += 4;
		uint codec = 0, rate = 0, samples = 0, ch = 1;
		size_t data = 0, coefs = 0;
		bool done = false;
		while (!done && p < sz)
		{
			const uint tag = raw[p++];
			if (tag == 0xff)
				break;
			if (tag == 0xfc || tag == 0xfd)
				continue;
			if (p >= sz)
				break;
			uint len = raw[p++];
			if (len == 0xff)
				break;
			if (p + len > sz)
				break;
			uint v = 0;
			if (len <= 4)
				for (uint k = 0; k < len; k++)
					v = (v << 8) | raw[p + k];
			switch (tag)
			{
			case 0xa0: codec = v; break;
			case 0x82: ch = v; break;
			case 0x84: rate = v; break;
			case 0x85: samples = v; break;
			case 0x88: data = v; break;
			case 0x8f: coefs = p; break;
			}
			p += len;
		}
		if (codec != 0x12 || ch != 1 || !coefs || !samples || !rate || coefs + 32 > sz)
			continue;
		const size_t frames = ((size_t)samples + 13) / 14;
		const size_t bytes = frames * 8;
		if (!data || data > sz || bytes > sz - data)
			continue;
		u8 *out = CALLOC (1, 0x60 + bytes);
		if (!out)
			continue;
		wr_be32 (out + 0x00, samples);
		wr_be32 (out + 0x04, frames * 16);
		wr_be32 (out + 0x08, rate);
		wr_be32 (out + 0x10, 2);
		wr_be32 (out + 0x14, frames * 16 - 1);
		wr_be32 (out + 0x18, 2);
		memcpy (out + 0x1c, raw + coefs, 32);
		out[0x3f] = raw[data];
		memcpy (out + 0x60, raw + data, bytes);
		char name[32];
		snprintf (name, sizeof (name), "%04u.dsp", i);
		if (!OwnedEntryAdd (entries, n, name, out, 0x60 + bytes))
		{
			FREE (out);
			break;
		}
		FREE (out);
		n++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (n)
		err = write_owned_entries (arg, basedir, depth, "EABNK", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
