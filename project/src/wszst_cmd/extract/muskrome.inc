// Krome Studios ".mus" streams (Star Wars: The Force Unleashed, Wii; also Ty 2 on GameCube; layout after vgmstream's
// "mus_krome"): big-endian u32 samples @0, u32 interleave 0x8000 @4, u32 data start 0x80 @8, u32 data size @0xc (data
// size + 0x80 is the file size), a DSP coefficient block per channel at 0x10 (+0x2e for the next channel: 0x20
// coefficients, gain, initial ps, history 1 / 2 at +0x24 / +0x26), u16 sample rate @0x6c. Stereo, DSP-ADPCM
// interleaved in 0x8000 byte blocks. Each channel is written as "chN.dsp" (decoded to WAV by the DSP extractor).
static enumError extract_muskrome_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".mus"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 samples = sz >= 0x80 ? rd_be32 (raw) : 0;
	if (sz < 0x80 || !samples || rd_be32 (raw + 4) != 0x8000 || rd_be32 (raw + 8) != 0x80
		|| (size_t)rd_be32 (raw + 0xc) + 0x80 != sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 rate = rd_be16 (raw + 0x6c);
	// bytes of one channel needed for the sample count (14 samples per 8 byte frame)
	const size_t per = (size_t)((samples + 13) / 14) * 8;
	const u32 nibbles = (samples / 14) * 16 + (samples % 14 ? samples % 14 + 2 : 0);
	nintendo_sarc_entry_t *entries = CALLOC (2, sizeof (*entries));
	u8 *out = CALLOC (1, 0x60 + per);
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
		memset (out, 0, 0x60 + per);
		const u8 *blk = raw + 0x10 + c * 0x2e;
		size_t done = 0;
		for (size_t blkpos = 0x80 + (size_t)c * 0x8000; done < per && blkpos < sz; blkpos += 0x10000)
		{
			size_t take = sz - blkpos < 0x8000 ? sz - blkpos : 0x8000;
			if (take > per - done)
				take = per - done;
			memcpy (out + 0x60 + done, raw + blkpos, take);
			done += take;
		}
		wr_be32 (out, samples);
		wr_be32 (out + 4, nibbles);
		wr_be32 (out + 8, rate);
		wr_be32 (out + 0x10, 2);
		wr_be32 (out + 0x14, nibbles - 1);
		wr_be32 (out + 0x18, 2);
		memcpy (out + 0x1c, blk, 0x20);
		out[0x3f] = out[0x60];
		memcpy (out + 0x40, blk + 0x24, 4);
		char name[16];
		snprintf (name, sizeof (name), "ch%u.dsp", c);
		if (!OwnedEntryAdd (entries, n, name, out, 0x60 + per))
			break;
		n++;
	}
	FREE (out);
	enumError err = ERR_NOTHING_TO_DO;
	if (n == 2)
		err = write_owned_entries (arg, basedir, depth, "KROME-MUS", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
