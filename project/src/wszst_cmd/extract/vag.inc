// Sony "VAGp" (PS-ADPCM) "*.vag": big-endian {"VAGp", u32 version, u32 0, u32 data bytes, u32 rate, 12 bytes, char name[16]}, 0x30-byte header, then 16-byte ADPCM frames {shift|filter<<4, flags, 14 bytes of nibbles}. Mono; decoded to "<file>.wav".
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
		static const int f0[5] = { 0, 60, 115, 98, 122 }, f1[5] = { 0, 0, -52, -55, -60 };
		int s1 = 0, s2 = 0;
		uint ns = 0;
		for (uint i = 0; i < nf; i++)
		{
			const u8 *fr = raw + 0x30 + (size_t)i * 16;
			const uint shift = fr[0] & 15, filt = fr[0] >> 4;
			if (filt > 4)
				goto done;
			if (fr[1] == 7)
				break;
			for (uint k = 0; k < 28; k++)
			{
				int v = k & 1 ? fr[2 + k / 2] >> 4 : fr[2 + k / 2] & 15;
				if (v > 7)
					v -= 16;
				int s = (v << 12 >> shift) + ((s1 * f0[filt] + s2 * f1[filt] + 32) >> 6);
				if (s > 32767) s = 32767;
				if (s < -32768) s = -32768;
				pcm[ns++] = (s16)s;
				s2 = s1;
				s1 = s;
			}
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
