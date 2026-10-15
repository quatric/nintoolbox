// Eurocom "MUSX" sound banks, Wii "SBNK" form (Dead Space: Extraction "File_<region>.000"; layout after vgmstream
// "musx"): "MUSX", u32 version 10 (little-endian) @8, platform "WII_" @0x10; an "SBNK" block at 0x800 (u32 0x12 version;
// the table block starts at 0x810 and is big-endian) whose table 5 (count @+0x20, offset @+0x24 from there + 0x24)
// lists the sounds as 0x1c-byte records {u32 id, s32 samples, s32 loop start (-1 = none), u16 rate, u8 codec, u8 channels,
// u32 data offset (+ data base @+0x3c), u32 size, ...}. Codec 0x13 (DSP-ADPCM) sounds hold one block of size / channels
// bytes per channel, each starting with a 0x40-byte "NGCA" header (u32 size, u32 2, coefficients from +0xc, gain
// +0x2c, initial ps +0x2e, history +0x30) followed by the ADPCM. Those become "NNNN_<id>[_chN].dsp"; other codecs
// (DAT4 IMA, PCM) and the ESPD project file are skipped.
static enumError extract_musx_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".000") && !is_ext (arg, ".musx") && !is_ext (arg, ".sfx"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x900 || memcmp (raw, "MUSX", 4) || rd_le32 (raw + 8) != 10 || memcmp (raw + 0x10, "WII_", 4)
		|| memcmp (raw + 0x800, "SBNK", 4) || rd_be32 (raw + 0x804) != 0x12)
		goto skip;
	const size_t to = 0x810, cnt = rd_be32 (raw + to + 0x20), head = rd_be32 (raw + to + 0x24) + to + 0x24,
		     data = rd_be32 (raw + to + 0x3c);
	if (!cnt || cnt > 0x10000 || head + 0x1c * cnt > sz)
		goto skip;
	nintendo_sarc_entry_t *entries = CALLOC (cnt * 2, sizeof (*entries));
	uint n = 0;
	bool good = entries != 0;
	for (size_t i = 0; i < cnt && good; i++)
	{
		const u8 *e = raw + head + 0x1c * i;
		const u32 id = rd_be32 (e);
		const s32 ns = (s32)rd_be32 (e + 4), ls = (s32)rd_be32 (e + 8);
		const uint rate = rd_be16 (e + 0xc), codec = e[0xe], ch = e[0xf];
		const size_t off = (size_t)rd_be32 (e + 0x10) + data, size = rd_be32 (e + 0x14);
		if (codec != 0x13 || ns <= 0 || !rate || ch < 1 || ch > 2 || off > sz || size > sz - off)
			continue;
		const size_t il = size / ch;
		if (il <= 0x40)
			continue;
		const size_t nibbles = (size_t)ns / 14 * 16 + ((size_t)ns % 14 ? (size_t)ns % 14 + 2 : 0), nb = (nibbles + 1) / 2;
		if (nb > il - 0x40)
			continue;
		const bool loop = ls >= 0 && ls < ns;
		for (uint c = 0; c < ch && good; c++)
		{
			const u8 *b = raw + off + il * c;
			if (memcmp (b, "NGCA", 4))
				continue;
			u8 *out = CALLOC (1, 0x60 + nb);
			if (!out)
			{
				good = false;
				break;
			}
			wr_be32 (out, (u32)ns);
			wr_be32 (out + 4, (u32)nibbles);
			wr_be32 (out + 8, rate);
			wr_be16 (out + 0x0c, loop);
			wr_be32 (out + 0x10, loop ? (u32)((u32)ls / 14 * 16 + 2 + (u32)ls % 14) : 2);
			wr_be32 (out + 0x14, loop ? (u32)(((u32)ns - 1) / 14 * 16 + 2 + ((u32)ns - 1) % 14) : (u32)nibbles - 1);
			wr_be32 (out + 0x18, 2);
			memcpy (out + 0x1c, b + 0x0c, 0x20);
			memcpy (out + 0x3c, b + 0x2c, 6);
			memcpy (out + 0x60, b + 0x40, nb);
			if (loop)
				out[0x45] = out[0x60 + ((u32)ls / 14) * 8];
			char name[48];
			if (ch == 1)
				snprintf (name, sizeof (name), "%04u_%08x.dsp", (uint)i, id);
			else
				snprintf (name, sizeof (name), "%04u_%08x_ch%u.dsp", (uint)i, id, c);
			good = OwnedEntryAdd (entries, n, name, out, 0x60 + nb);
			FREE (out);
			if (good)
				n++;
		}
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (good && n)
		err = write_owned_entries (arg, basedir, depth, "MUSX", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
skip:
	FREE (raw);
	return ERR_NOTHING_TO_DO;
}
