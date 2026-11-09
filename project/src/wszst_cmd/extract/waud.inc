// "waud" raw PCM sound (Cook Wars / Cooking Party / Imagine - Fashion Idol ...: Sound/*.wtxs effects, *.wtss music; any extension).
// 32 byte header {"waud", u8 bytes per sample (1 = unsigned 8 bit, 2 = big endian s16), u8 channels, u8 flag (1 on the streamed .wtss),
// u8 0, u24be sample rate, u8 0, u16be data size in 256 byte pages, u16 flags, zero padding}; the samples (interleaved when stereo) follow
// to the end of the file. Written as "<file>.wav".
static enumError extract_waud_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	const uint bps = sz > 6 ? raw[4] : 0, ch = sz > 6 ? raw[5] : 0;
	const u32 rate = sz > 12 ? (rd_be32 (raw + 8) >> 8) : 0;
	if (sz < 0x22 || memcmp (raw, "waud", 4) || (bps != 1 && bps != 2) || (ch != 1 && ch != 2) || rate < 4000 || rate > 96000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t n = (sz - 32) / (bps * ch) * (bps * ch);
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".wav");
	if (testmode)
	{
		fprintf (stdlog, "WOULD EXTRACT WAUD:%s -> WAV:%s\n", arg, dest);
		FREE (raw);
		return ERR_OK;
	}
	u8 *w = MALLOC (44 + n);
	if (!w)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	const u32 hdr[] = { 16, (ch << 16) | 1, rate, rate * ch * bps, ((bps * 8) << 16) | (ch * bps) };
	memcpy (w, "RIFF", 4);
	wr_le32 (w + 4, (u32)(36 + n));
	memcpy (w + 8, "WAVEfmt ", 8);
	for (uint k = 0; k < 5; k++)
		wr_le32 (w + 16 + 4 * k, hdr[k]);
	memcpy (w + 36, "data", 4);
	wr_le32 (w + 40, (u32)n);
	if (bps == 2)
		for (size_t i = 0; i < n; i += 2)
			w[44 + i] = raw[32 + i + 1], w[45 + i] = raw[32 + i];
	else
		memcpy (w + 44, raw + 32, n);
	FREE (raw);
	File_t F;
	enumError err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f && fwrite (w, 1, 44 + n, F.f) != 44 + n)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", 44 + n, dest);
	ResetFile (&F, opt_preserve);
	FREE (w);
	if (!err && verbose >= 0)
		fprintf (stdlog, "%sEXTRACT WAUD:%s -> WAV:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}
