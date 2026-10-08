// Electronic Arts GameCube/Wii streams ".ngc" ("SCHl"/"SCCl"/"SCDl" blocks,
// little-endian {tag, u32 size incl. 8-byte header}; used by Tiger Woods PGA
// Tour movies and music). The SCHl header carries patch elements: after 0xFD,
//   0x80 revision, 0x82 channels, 0x83 codec, 0x84 sample rate, 0x85 samples
// each as {u8 id, u8 len, big-endian value}, terminated by 0xFF.
// Each SCDl holds one ADPCM "R3" packet (big-endian):
//   u32 samples, u32 channel_offset[ch] (relative to packet start + 4*(ch+1)),
//   then per channel 28-sample frames: byte 0xEE = raw {s16 cur, s16 prev,
//   28 * s16}, else {u8 coef<<4|shift, 14 nibble bytes}.
// Video (MVhd / "m6PV") blocks are skipped. The audio is written as WAV.
static const int ea_r3_table[20] = {0, 240, 460, 392, 0, 0, -208, -220, 0, 1, 3, 4, 7, 8, 10, 11, 0, -1, -3, -4};

static uint ea_patch_value (const u8 *p, uint len)
{
	uint v = 0;
	for (uint i = 0; i < len && i < 4; i++)
		v = (v << 8) | p[i];
	return v;
}

// decode one SCHl stream into a WAV image (caller frees *wav_out)
static enumError ea_decode_wav (const u8 *raw, size_t raw_size, u8 **wav_out, uint *wav_size, uint *ch_out, uint *rate_out)
{
	if (raw_size < 0x30 || memcmp (raw, "SCHl", 4) || rd_le32 (raw + 4) > raw_size)
		return ERR_NOTHING_TO_DO;

	// header patch elements
	uint rev = 0, channels = 1, rate = 48000;
	{
		const size_t hend = rd_le32 (raw + 4);
		size_t p = 8;
		while (p + 2 < hend && raw[p] != 0xfd)
			p++;
		for (p++; p + 2 <= hend && raw[p] != 0xff;)
		{
			const uint id = raw[p], len = raw[p + 1];
			if (p + 2 + len > hend)
				break;
			const uint v = ea_patch_value (raw + p + 2, len);
			if (id == 0x80)
				rev = v;
			else if (id == 0x82)
				channels = v;
			else if (id == 0x84)
				rate = v;
			p += 2 + len;
		}
	}
	if (rev != 3 || channels < 1 || channels > 6 || rate < 4000 || rate > 192000)
	{
		return ERR_NOTHING_TO_DO;
	}

	size_t cap = 1 << 20, used = 0;
	s16 *pcm = MALLOC (cap * sizeof (s16));
	if (!pcm)
	{
		return ERR_OUT_OF_MEMORY;
	}
	int prev[6] = {0}, cur[6] = {0};
	s16 *chan = MALLOC (65536 * sizeof (s16));
	if (!chan)
	{
		FREE (pcm);
		return ERR_OUT_OF_MEMORY;
	}
	for (size_t p = 0; p + 8 <= raw_size;)
	{
		const size_t sz = rd_le32 (raw + p + 4);
		if (sz < 8 || sz > raw_size - p)
			break;
		if (!memcmp (raw + p, "SCDl", 4) && sz >= 8 + 4 + 4 * channels)
		{
			const u8 *b = raw + p + 8;
			const size_t bsz = sz - 8;
			uint ns = rd_be32 (b);
			ns -= ns % 28;
			if (ns > 65536)
				ns = 65536 - 65536 % 28;
			bool ok = true;
			for (uint c = 0; c < channels && ok; c++)
			{
				size_t o = (size_t)rd_be32 (b + 4 + 4 * c) + (channels + 1) * 4;
				for (uint f = 0; f < ns / 28 && ok; f++)
				{
					if (o >= bsz)
					{
						ok = false;
						break;
					}
					uint byte = b[o++];
					if (byte == 0xee)
					{
						if (o + 4 + 56 > bsz)
						{
							ok = false;
							break;
						}
						cur[c] = (s16)((b[o] << 8) | b[o + 1]);
						prev[c] = (s16)((b[o + 2] << 8) | b[o + 3]);
						o += 4;
						for (uint k = 0; k < 28; k++, o += 2)
							chan[f * 28 + k] = (s16)((b[o] << 8) | b[o + 1]);
					}
					else
					{
						if (o + 14 > bsz)
						{
							ok = false;
							break;
						}
						const int c1 = ea_r3_table[byte >> 4], c2 = ea_r3_table[(byte >> 4) + 4];
						const uint shift = 20 - (byte & 15);
						for (uint k = 0; k < 28; k++)
						{
							int nib;
							if (k & 1)
								nib = b[o++] & 15;
							else
								nib = b[o] >> 4;
							nib = (nib ^ 8) - 8;
							int64_t ns64 = (int64_t)((u32)nib << shift) + (int64_t)cur[c] * c1 + (int64_t)prev[c] * c2;
							ns64 = (int32_t)ns64 >> 8;
							if (ns64 > 32767)
								ns64 = 32767;
							else if (ns64 < -32768)
								ns64 = -32768;
							prev[c] = cur[c];
							cur[c] = (int)ns64;
							chan[f * 28 + k] = (s16)ns64;
						}
					}
				}
				if (ok)
				{
					// store this channel's samples interleaved
					if (used + (size_t)ns * channels > cap)
					{
						while (used + (size_t)ns * channels > cap)
							cap *= 2;
						s16 *np = REALLOC (pcm, cap * sizeof (s16));
						if (!np)
						{
							ok = false;
							break;
						}
						pcm = np;
					}
					// channels decode in order into a staging area: keep per-block layout
					for (uint k = 0; k < ns; k++)
						pcm[used + (size_t)k * channels + c] = chan[k];
				}
			}
			if (ok)
				used += (size_t)ns * channels;
		}
		p += sz;
	}
	FREE (chan);
	if (!used)
	{
		FREE (pcm);
		return ERR_NOTHING_TO_DO;
	}

	const size_t data = used * 2;
	u8 *wav = MALLOC (44 + data);
	if (!wav)
	{
		FREE (pcm);
		return ERR_OUT_OF_MEMORY;
	}
	memcpy (wav, "RIFF", 4);
	wr_le32 (wav + 4, (u32)(36 + data));
	memcpy (wav + 8, "WAVEfmt ", 8);
	wr_le32 (wav + 16, 16);
	wr_le16 (wav + 20, 1);
	wr_le16 (wav + 22, channels);
	wr_le32 (wav + 24, rate);
	wr_le32 (wav + 28, rate * channels * 2);
	wr_le16 (wav + 32, channels * 2);
	wr_le16 (wav + 34, 16);
	memcpy (wav + 36, "data", 4);
	wr_le32 (wav + 40, (u32)data);
	for (size_t i = 0; i < used; i++)
		wr_le16 (wav + 44 + 2 * i, (u16)pcm[i]);
	FREE (pcm);

	*wav_out = wav;
	*wav_size = (uint)(44 + data);
	*ch_out = channels;
	*rate_out = rate;
	return ERR_OK;
}

static enumError extract_eangc_file (ccp arg, ccp basedir, uint depth)
{
	const bool ngc = is_ext (arg, ".ngc"), fcd = is_ext (arg, ".fcd");
	if (!ngc && !fcd)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw_size < 0x30 || memcmp (raw, "SCHl", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	enumError err = ERR_NOTHING_TO_DO;
	uint n = 0;
	// ".fcd" data files (NBA Live speech) concatenate many SCHl..SCEl streams
	for (size_t p = 0; p + 8 <= raw_size;)
	{
		size_t e = raw_size;
		if (fcd)
		{
			size_t q = p;
			bool found = false;
			while (q + 8 <= raw_size)
			{
				const size_t sz = rd_le32 (raw + q + 4);
				if (sz < 8 || sz > raw_size - q)
					break;
				q += sz;
				if (!memcmp (raw + q - sz, "SCEl", 4))
				{
					found = true;
					break;
				}
			}
			if (!found)
				break;
			e = q;
		}
		u8 *wav = 0;
		uint wsz = 0, ch = 0, rate = 0;
		enumError e2 = ea_decode_wav (raw + p, e - p, &wav, &wsz, &ch, &rate);
		if (e2 == ERR_OK)
		{
			char name[64];
			if (fcd)
				snprintf (name, sizeof (name), "%05u.wav", n);
			else
				snprintf (name, sizeof (name), "audio.wav");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT EA-NGC:%s (%u ch, %u Hz) -> %s/%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", arg, ch, rate, dest, name);
			err = ERR_OK;
			if (!testmode)
				err = shade_write (arg, dest, basedir, name, wav, wsz);
			n++;
		}
		else if (e2 != ERR_NOTHING_TO_DO)
			err = e2;
		FREE (wav);
		if (!fcd || err > ERR_WARNING)
			break;
		p = e;
		while (p + 4 <= raw_size && memcmp (raw + p, "SCHl", 4))
			p++;
	}
	FREE (raw);
	return n ? ERR_OK : err;
}
