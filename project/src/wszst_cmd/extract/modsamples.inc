// ProTracker-style ".mod" modules (4 / 6 / 8 / 16 / 32 channel tags "M.K.", "M!K!", "FLT4", "FLT8", "4CHN", "6CHN", "8CHN", "16CN", "32CN"): every non-empty sample (31 headers of
// {22 byte name, u16 length in words, finetune, volume, u16 loop start, u16 loop length} at 20, order table at 950, tag at 1080, patterns of 64 rows x channels x 4 bytes,
// then signed 8 bit sample data) is written to "<file>.d/sample_NN.wav" (mono, 8 bit, 8363 Hz) and "info.txt" lists title, channels, orders and samples.
static enumError extract_modsamples_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".mod") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 1084)
	{
		const u8 *tag = raw + 1080;
		uint ch = 0;
		if (!memcmp (tag, "M.K.", 4) || !memcmp (tag, "M!K!", 4) || !memcmp (tag, "FLT4", 4) || !memcmp (tag, "4CHN", 4))
			ch = 4;
		else if (!memcmp (tag, "6CHN", 4))
			ch = 6;
		else if (!memcmp (tag, "8CHN", 4) || !memcmp (tag, "FLT8", 4) || !memcmp (tag, "OCTA", 4))
			ch = 8;
		else if (tag[2] == 'C' && tag[3] == 'H' && tag[0] >= '0' && tag[0] <= '9' && tag[1] >= '0' && tag[1] <= '9')
			ch = (tag[0] - '0') * 10 + tag[1] - '0';
		else if (tag[2] == 'C' && tag[3] == 'N' && tag[0] >= '0' && tag[0] <= '9' && tag[1] >= '0' && tag[1] <= '9')
			ch = (tag[0] - '0') * 10 + tag[1] - '0';
		if (ch)
		{
			uint pats = 0;
			for (uint i = 0; i < 128; i++)
				if (raw[952 + i] >= pats)
					pats = raw[952 + i] + 1;
			size_t pos = 1084 + (size_t)pats * 64 * ch * 4;
			char title[24];
			retro_text (title, raw, 20);
			retro_info_t info = {{0}, 0};
			retro_info (&info, "format: Amiga module (%s)\ntitle: %s\nchannels: %u\norders: %u\npatterns: %u\n", (char[5]){tag[0], tag[1], tag[2], tag[3], 0}, title, ch, raw[950], pats);
			char dest[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			err = ERR_OK;
			for (uint s = 0; s < 31 && err == ERR_OK; s++)
			{
				const u8 *h = raw + 20 + s * 30;
				const size_t len = (size_t)(h[22] << 8 | h[23]) * 2;
				char nm[24];
				retro_text (nm, h, 22);
				if (len > 2 && pos + len <= sz)
				{
					retro_info (&info, "sample %02u: \"%s\" %zu bytes, finetune %u, volume %u, loop %u+%u words\n", s, nm, len, h[24] & 15, h[25], h[26] << 8 | h[27], h[28] << 8 | h[29]);
					u8 *pcm = MALLOC (len);
					if (pcm)
					{
						for (size_t i = 0; i < len; i++)
							pcm[i] = raw[pos + i] ^ 0x80;
						u8 *wav = MALLOC (44 + len);
						if (wav)
						{
							memcpy (wav, "RIFF", 4); pcm_put32 (wav + 4, 36 + len);
							memcpy (wav + 8, "WAVEfmt ", 8); pcm_put32 (wav + 16, 16);
							wav[20] = 1; wav[21] = 0; wav[22] = 1; wav[23] = 0;
							pcm_put32 (wav + 24, 8363); pcm_put32 (wav + 28, 8363);
							wav[32] = 1; wav[33] = 0; wav[34] = 8; wav[35] = 0;
							memcpy (wav + 36, "data", 4); pcm_put32 (wav + 40, len);
							memcpy (wav + 44, pcm, len);
							char fn[64];
							snprintf (fn, sizeof (fn), "sample_%02u.wav", s);
							if (verbose >= 0 || testmode)
								fprintf (stdlog, "%s%sEXTRACT MOD:%s -> %s/%s%s (%zu bytes)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, basedir ? basedir : "", fn, len);
							err = shade_write (arg, dest, basedir, fn, wav, (uint)(44 + len));
							FREE (wav);
						}
						FREE (pcm);
					}
				}
				pos += len;
			}
			if (!err)
				err = shade_write (arg, dest, basedir, "info.txt", (u8 *)info.buf, (uint)info.n);
		}
	}
	FREE (raw);
	return err;
}
