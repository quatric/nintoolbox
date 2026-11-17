// Dance Sensation! / related (Wii) "Audio/music/*.lcm": headerless big-endian signed 16-bit mono PCM music (the companion ".AMF" holds beat data, not format information). The 32000 Hz rate is
// inferred (the game's main.dol contains 32000 but no other common audio rate); it is not stored in the file. Written as "<file>.wav". A content check (neighbouring samples are closer than
// samples two apart, i.e. mono PCM and not ADPCM / stereo) guards against other data using the ".lcm" extension.
static enumError extract_lcmpcm_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".lcm"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x10000 || (sz & 1))
		goto done;
	{
		u64 d1 = 0, d2 = 0, amp = 0;
		const size_t n = sz / 2;
		for (size_t off = n / 4; off + 20002 < n && off < n / 4 + 80000; off += 4001)
			for (uint i = 2; i < 4000; i++)
			{
				const int a = (s16)rd_be16 (raw + (off + i) * 2), b = (s16)rd_be16 (raw + (off + i - 1) * 2), c = (s16)rd_be16 (raw + (off + i - 2) * 2);
				d1 += abs (a - b);
				d2 += abs (a - c);
				amp += abs (a);
			}
		if (!amp || d1 * 10 >= d2 * 9)
			goto done;
		char wav[PATH_MAX];
		snprintf (wav, sizeof (wav), "%s.wav", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE LCM:%s (PCM16 mono, %u samples, 32000 Hz assumed) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, (uint)n, wav);
		err = ERR_OK;
		if (!testmode)
		{
			u8 h[44];
			memcpy (h, "RIFF", 4);
			wr_le32 (h + 4, (u32)(36 + sz));
			memcpy (h + 8, "WAVEfmt ", 8);
			wr_le32 (h + 16, 16);
			h[20] = 1, h[21] = 0, h[22] = 1, h[23] = 0;
			wr_le32 (h + 24, 32000);
			wr_le32 (h + 28, 64000);
			h[32] = 2, h[33] = 0, h[34] = 16, h[35] = 0;
			memcpy (h + 36, "data", 4);
			wr_le32 (h + 40, (u32)sz);
			for (size_t i = 0; i + 1 < sz; i += 2)
			{
				const u8 t = raw[i];
				raw[i] = raw[i + 1];
				raw[i + 1] = t;
			}
			File_t F;
			err = CreateFileOpt (&F, true, wav, false, arg);
			if (F.f && (fwrite (h, 1, 44, F.f) != 44 || fwrite (raw, 1, sz, F.f) != sz))
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)sz, wav);
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (raw);
	return err;
}
