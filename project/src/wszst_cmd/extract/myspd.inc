// Punchers Impact ".myspd" (U-Sing, Wii): u32 BE channel size, u32 BE rate, header padded to 0x20,
// then two channels of plain IMA ADPCM (low nibble first, history 0, step index 0), one after the
// other. The file size is exactly 0x20 + 2 * channel size. -> stereo.wav
static enumError extract_myspd_file (ccp arg, ccp basedir, uint depth)
{
	static const s16 step_tab[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73,
		80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
		963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
		7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
	static const s8 idx_tab[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
	if (!is_ext (arg, ".myspd"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t cs = sz >= 0x20 ? rd_be32 (raw) : 0;
	const u32 rate = sz >= 0x20 ? rd_be32 (raw + 4) : 0;
	if (!cs || !rate || cs > sz || 0x20 + 2 * cs != sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t frames = cs * 2, data = frames * 4;
	u8 *wav = MALLOC (44 + data);
	nintendo_sarc_entry_t *entries = wav ? CALLOC (1, sizeof (*entries)) : 0;
	if (!entries)
	{
		FREE (wav);
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	memcpy (wav, "RIFF", 4);
	wr_le32 (wav + 4, (u32)(36 + data));
	memcpy (wav + 8, "WAVEfmt ", 8);
	wr_le32 (wav + 16, 16);
	wr_le16 (wav + 20, 1);
	wr_le16 (wav + 22, 2);
	wr_le32 (wav + 24, rate);
	wr_le32 (wav + 28, rate * 4);
	wr_le16 (wav + 32, 4);
	wr_le16 (wav + 34, 16);
	memcpy (wav + 36, "data", 4);
	wr_le32 (wav + 40, (u32)data);
	for (uint c = 0; c < 2; c++)
	{
		int hist = 0, index = 0;
		for (size_t i = 0; i < frames; i++)
		{
			const int code = (raw[0x20 + c * cs + i / 2] >> (4 * (i & 1))) & 15, step = step_tab[index];
			int delta = step >> 3;
			if (code & 1)
				delta += step >> 2;
			if (code & 2)
				delta += step >> 1;
			if (code & 4)
				delta += step;
			if (code & 8)
				delta = -delta;
			hist += delta;
			hist = hist < -32768 ? -32768 : hist > 32767 ? 32767 : hist;
			index += idx_tab[code];
			index = index < 0 ? 0 : index > 88 ? 88 : index;
			wr_le16 (wav + 44 + 4 * i + 2 * c, (u16)hist);
		}
	}
	enumError err = ERR_OUT_OF_MEMORY;
	if (OwnedEntryAdd (entries, 0, "stereo.wav", wav, 44 + data))
		err = write_owned_entries (arg, basedir, depth, "MYSPD", entries, 1);
	else
		FREE (entries);
	FREE (wav);
	FREE (raw);
	return err;
}
