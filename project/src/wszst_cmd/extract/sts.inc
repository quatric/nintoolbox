// Alfa System ".sts" voice / sound files (Castle of Shikigami III, Wii), layout after vgmstream's "sts": u32 BE
// size without the leading word (the file is exactly 4 bytes longer), u8 channels - 1 @8, u16 sample rate @0x0c, u32 bytes
// per channel @0x1a, then a DSP coefficient / history block at 0x1e (0x20 bytes of coefficients, history 1 and 2 at +0x24
// / +0x26). Mono data starts at 0x70, stereo data at 0x50; every further channel is a 0x2e-byte block of that kind followed
// by the channel's ADPCM data, so the channel stride is bytes per channel + 0x2e. Each channel is written as "chN.dsp"
// (decoded to WAV by the DSP extractor).
static enumError extract_sts_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".sts"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x70 || (size_t)rd_be32 (raw) + 4 != sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint ch = raw[8] + 1, rate = rd_be16 (raw + 0x0c);
	const size_t per = rd_be32 (raw + 0x1a), start = ch == 1 ? 0x70 : 0x50;
	if (ch > 2 || !rate || per < 8 || start + (ch - 1) * (per + 0x2e) + 8 > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	// dsp_bytes_to_samples: 14 samples per 8 byte frame, a partial frame holds (bytes - 1) * 2
	const size_t frames = per / 8, extra = per % 8;
	const u32 samples = (u32)(frames * 14 + (extra ? (extra - 1) * 2 : 0));
	nintendo_sarc_entry_t *entries = CALLOC (ch, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	for (uint c = 0; c < ch; c++)
	{
		const size_t blk = c == 0 ? 0x1e : start + per + (c - 1) * (per + 0x2e), data = start + c * (per + 0x2e);
		u8 *out = CALLOC (1, 0x60 + per);
		if (!out || blk + 0x28 > sz || data >= sz)
		{
			FREE (out);
			break;
		}
		wr_be32 (out, samples);
		wr_be32 (out + 4, (samples / 14) * 16 + (samples % 14 ? samples % 14 + 2 : 0));
		wr_be32 (out + 8, rate);
		wr_be32 (out + 0x10, 2);
		wr_be32 (out + 0x14, (samples / 14) * 16 + (samples % 14 ? samples % 14 + 2 : 0) - 1);
		wr_be32 (out + 0x18, 2);
		memcpy (out + 0x1c, raw + blk, 0x20);
		out[0x3f] = raw[data];
		memcpy (out + 0x40, raw + blk + 0x24, 4);
		memcpy (out + 0x60, raw + data, data + per > sz ? sz - data : per);
		char name[16];
		snprintf (name, sizeof (name), "ch%u.dsp", c);
		const bool added = OwnedEntryAdd (entries, n, name, out, 0x60 + per);
		FREE (out);
		if (!added)
			break;
		n++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (n == ch)
		err = write_owned_entries (arg, basedir, depth, "STS", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
