// Rebellion (Asura engine) ".sfx" stereo DSP streams (Sniper Elite Wii; also music): big-endian
// {u32 0, u32 2, u32 2, u32 nibbles, u32 rate, u32 ch0 offset, u32 ch1 offset}; every channel
// block is {16 x s16 coefs, ... u16 gain @+0x20, u16 initial ps @+0x22, ADPCM data @+0x30}.
// Each channel is rebuilt as a standard DSP ("ch0.dsp", "ch1.dsp").
static enumError extract_asurasfx_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".sfx"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x80 || rd_be32 (raw) || rd_be32 (raw + 4) != 2 || rd_be32 (raw + 8) != 2)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 nibbles = rd_be32 (raw + 0x0c), rate = rd_be32 (raw + 0x10);
	const size_t off[2] = {rd_be32 (raw + 0x14), rd_be32 (raw + 0x18)};
	const size_t bytes = ((size_t)nibbles + 1) / 2;
	const size_t gap = off[1] - off[0];
	if (!nibbles || rate < 4000 || rate > 192000 || off[1] <= off[0] || gap < bytes || off[1] + 0x30 + bytes > sz
		|| rd_be16 (raw + off[0] + 0x22) != raw[off[0] + 0x30] || rd_be16 (raw + off[1] + 0x22) != raw[off[1] + 0x30])
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 samples = nibbles / 16 * 14 + (nibbles % 16 > 2 ? nibbles % 16 - 2 : 0);
	nintendo_sarc_entry_t *entries = CALLOC (2, sizeof (*entries));
	u8 *out = CALLOC (1, 0x60 + bytes);
	if (!entries || !out)
	{
		FREE (entries);
		FREE (out);
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	for (uint c = 0; c < 2; c++)
	{
		memset (out, 0, 0x60 + bytes);
		wr_be32 (out + 0x00, samples);
		wr_be32 (out + 0x04, nibbles);
		wr_be32 (out + 0x08, rate);
		wr_be32 (out + 0x10, 2);
		wr_be32 (out + 0x14, nibbles - 1);
		wr_be32 (out + 0x18, 2);
		memcpy (out + 0x1c, raw + off[c], 32);
		out[0x3f] = raw[off[c] + 0x30];
		memcpy (out + 0x60, raw + off[c] + 0x30, bytes);
		char name[16];
		snprintf (name, sizeof (name), "ch%u.dsp", c);
		if (!OwnedEntryAdd (entries, n, name, out, 0x60 + bytes))
			break;
		n++;
	}
	FREE (out);
	enumError err = ERR_NOTHING_TO_DO;
	if (n == 2)
		err = write_owned_entries (arg, basedir, depth, "ASURA-SFX", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
