// Sony "VAGp" (PS-ADPCM) "*.vag": big-endian {"VAGp", u32 version, u32 0, u32 data bytes, u32 rate, 12 bytes, char name[16]}, 0x30-byte header, then 16-byte ADPCM frames {shift|filter<<4, flags, 14 bytes of nibbles}. Mono; decoded to "<file>.wav".
// One 16-byte PS-ADPCM frame {shift|filter<<4, flags, 14 bytes of nibbles} -> 28 samples written every STRIDE s16; the filter must be <= 4.
static void psx_adpcm_frame (const u8 *fr, s16 *out, uint stride, int *s1, int *s2)
{
	static const int f0[5] = { 0, 60, 115, 98, 122 }, f1[5] = { 0, 0, -52, -55, -60 };
	const uint shift = fr[0] & 15, filt = fr[0] >> 4;
	for (uint k = 0; k < 28; k++)
	{
		int v = k & 1 ? fr[2 + k / 2] >> 4 : fr[2 + k / 2] & 15;
		if (v > 7)
			v -= 16;
		int s = (v << 12 >> shift) + ((*s1 * f0[filt] + *s2 * f1[filt] + 32) >> 6);
		if (s > 32767) s = 32767;
		if (s < -32768) s = -32768;
		out[k * stride] = (s16)s;
		*s2 = *s1;
		*s1 = s;
	}
}

static enumError extract_vag_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir; (void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".vag") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	s16 *pcm = 0;
	if (sz < 0x50 || memcmp (raw, "VAGp", 4))
		goto done;
	{
		u32 len = rd_be32 (raw + 12);
		const u32 rate = rd_be32 (raw + 16);
		if (!rate || rate > 192000)
			goto done;
		if (len > sz - 0x30)
			len = (u32)(sz - 0x30);
		const uint nf = len / 16;
		if (!nf)
			goto done;
		pcm = MALLOC ((size_t)nf * 28 * sizeof (s16));
		int s1 = 0, s2 = 0;
		uint ns = 0;
		for (uint i = 0; i < nf; i++)
		{
			const u8 *fr = raw + 0x30 + (size_t)i * 16;
			if ((fr[0] >> 4) > 4)
				goto done;
			if (fr[1] == 7)
				break;
			psx_adpcm_frame (fr, pcm + ns, 1, &s1, &s2);
			ns += 28;
		}
		if (!ns)
			goto done;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s.wav", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT VAG:%s (%u samples @ %u Hz) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ns, rate, path);
		err = testmode ? ERR_OK : spx_write_wav (arg, path, pcm, ns, 1, rate);
	}
done:
	FREE (pcm);
	FREE (raw);
	return err;
}

// Sony PS2-style "SShd"/"SSbd" streams "*.ads" (Aliens in the Attic Wii music/voice): little-endian {"SShd", u32 0x18, u32 codec (0x10 PS-ADPCM, 1 PCM16), u32 rate, u32 channels, u32 interleave,
// u32 loop start, u32 loop end} then {"SSbd", u32 data bytes} and the data at 0x28, channels interleaved in INTERLEAVE byte blocks. Decoded to "<file>.wav".
static enumError extract_ads_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir; (void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".ads") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	s16 *pcm = 0;
	if (sz < 0x28 || memcmp (raw, "SShd", 4) || memcmp (raw + 0x20, "SSbd", 4) || rd_le32 (raw + 4) != 0x18)
		goto done;
	{
		const u32 codec = rd_le32 (raw + 8), rate = rd_le32 (raw + 12), ch = rd_le32 (raw + 16), il = rd_le32 (raw + 20);
		u32 len = rd_le32 (raw + 0x24);
		if ((codec != 0x10 && codec != 1) || !rate || rate > 192000 || !ch || ch > 8 || !il || il % 16 || il > 0x100000)
			goto done;
		if (len > sz - 0x28)
			len = (u32)(sz - 0x28);
		const uint blocks = len / (il * ch); // whole interleave rows only
		const uint per_block = codec == 1 ? il / 2 : il / 16 * 28;
		const size_t ns = (size_t)blocks * per_block;
		if (!ns)
			goto done;
		pcm = MALLOC (ns * ch * sizeof (s16));
		int st[8][2] = { { 0 } };
		for (uint b = 0; b < blocks; b++)
			for (uint c = 0; c < ch; c++)
			{
				const u8 *blk = raw + 0x28 + ((size_t)b * ch + c) * il;
				s16 *out = pcm + ((size_t)b * per_block) * ch + c;
				if (codec == 1)
					for (uint k = 0; k < per_block; k++)
						out[k * ch] = (s16)rd_le16 (blk + 2 * k);
				else
					for (uint f = 0; f < il / 16; f++)
					{
						if ((blk[f * 16] >> 4) > 4)
							goto done;
						psx_adpcm_frame (blk + f * 16, out + (size_t)f * 28 * ch, ch, &st[c][0], &st[c][1]);
					}
			}
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s.wav", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT ADS:%s (%u ch, %zu samples @ %u Hz) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, ns, rate, path);
		err = testmode ? ERR_OK : spx_write_wav (arg, path, pcm, ns, ch, rate);
	}
done:
	FREE (pcm);
	FREE (raw);
	return err;
}
