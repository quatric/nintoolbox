// Telltale Games (Back to the Future: The Game, Sam & Max, ...; Wii) "*.vox" voice lines: "ERTM" {u32 version 1, ...}, at 0x1c a byte 0 and then u32 LE frame size (640), sample rate (32000), Speex mode (0 narrow, 1 wide,
// 2 ultra-wide), table byte size + 8, frame count N, N u32 LE frame start offsets (relative to the end of the table); every frame is one raw Speex frame (no Ogg framing, variable length, the last one runs to the end of the
// file). Decoded with the bundled libspeex to "<file>.wav".
static enumError extract_ertmvox_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".vox") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 || memcmp (raw, "ERTM", 4) || raw[0x1c])
		goto done;
	{
		const uint fsz = rd_le32 (raw + 0x1d), rate = rd_le32 (raw + 0x21), mode_id = rd_le32 (raw + 0x25), tab = rd_le32 (raw + 0x29), n = rd_le32 (raw + 0x2d);
		if (!n || n > 100000 || tab != 4 * n + 8 || mode_id > 2 || rate < 4000 || rate > 96000 || fsz < 80 || fsz > 4096 || 0x31 + (size_t)n * 4 > sz)
			goto done;
		const size_t base = 0x31 + (size_t)n * 4;
		const SpeexMode *mode = speex_lib_get_mode ((int)mode_id);
		void *st = mode ? speex_decoder_init (mode) : 0;
		if (!st)
			goto done;
		int enh = 1, frame = 0;
		speex_decoder_ctl (st, SPEEX_SET_ENH, &enh);
		speex_decoder_ctl (st, SPEEX_GET_FRAME_SIZE, &frame);
		if ((uint)frame != fsz)
		{
			speex_decoder_destroy (st);
			goto done;
		}
		s16 *pcm = (s16 *)CALLOC ((size_t)n * frame + 1, sizeof (s16));
		SpeexBits bits;
		speex_bits_init (&bits);
		uint ns = 0;
		for (uint i = 0; i < n && pcm; i++)
		{
			const size_t a = base + rd_le32 (raw + 0x31 + (size_t)i * 4), e = i + 1 < n ? base + rd_le32 (raw + 0x31 + (size_t)(i + 1) * 4) : sz;
			if (a >= e || e > sz)
				break;
			speex_bits_read_from (&bits, (char *)raw + a, (int)(e - a));
			if (speex_decode_int (st, &bits, pcm + (size_t)ns) < 0)
				break;
			ns += frame;
		}
		speex_bits_destroy (&bits);
		speex_decoder_destroy (st);
		if (pcm && ns)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s.wav", arg);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE SPEEX:%s (%u samples, %u Hz) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ns, rate, path);
			err = ERR_OK;
			if (!testmode)
				err = spx_write_wav (arg, path, pcm, ns, 1, rate);
		}
		FREE (pcm);
	}
done:
	FREE (raw);
	return err;
}
