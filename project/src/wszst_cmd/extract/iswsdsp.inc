// Codemasters "iSWS" streams (Dirt 2, Wii: Audio/wav/**/*.was), big-endian: {"iSWS", u32 0x18, u32 channels (1 or 2), ...}, one standard 0x60-byte DSP-ADPCM header per channel from 0x20 (samples,
// nibbles, rate, ..., coef[16] at +0x1c, hist at +0x40), the ADPCM data at 0x20 + 0x60 * channels with the 8-byte frames of the channels interleaved one by one (L, R, L, R, ...). Written as "<file>.d/<name>.wav".
static enumError extract_iswsdsp_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".was"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	s16 *pcm = 0;
	const uint ch = sz > 0x100 ? rd_be32 (raw + 8) : 0;
	if (sz < 0x100 || memcmp (raw, "iSWS", 4) || rd_be32 (raw + 4) != 0x18 || (ch != 1 && ch != 2))
		goto done;
	{
		const size_t ds = 0x20 + 0x60 * (size_t)ch;
		const u32 ns = rd_be32 (raw + 0x20), nib = rd_be32 (raw + 0x24), rate = rd_be32 (raw + 0x28);
		const size_t fr = (nib + 15) / 16;
		if (!ns || rate < 4000 || rate > 96000 || ds + fr * 8 * ch > sz + 7 || (u64)nib * 14 / 16 > (u64)ns + 14 + 1 || ds >= sz)
			goto done;
		char dest[PATH_MAX], name[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.d", arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".wav");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE ISWS:%s (%u ch, %u Hz, %u samples) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, ns, dest, name);
		err = ERR_OK;
		if (!testmode)
		{
			pcm = CALLOC (fr * 14 * ch, sizeof (s16));
			if (!pcm)
			{
				err = ERR_OUT_OF_MEMORY;
				goto done;
			}
			for (uint c = 0; c < ch; c++)
			{
				const u8 *h = raw + 0x20 + 0x60 * (size_t)c;
				s16 coef[16];
				for (uint k = 0; k < 16; k++)
					coef[k] = (s16)rd_be16 (h + 0x1c + 2 * k);
				int h1 = (s16)rd_be16 (h + 0x40), h2 = (s16)rd_be16 (h + 0x42);
				for (size_t f = 0; f < fr; f++)
				{
					const size_t o = ds + (f * ch + c) * 8;
					if (o + 8 > sz)
						break;
					const u8 *src = raw + o;
					const int sc = 1 << (src[0] & 15), ci = (src[0] >> 4) & 7;
					for (uint k = 0; k < 14; k++)
					{
						int v = (src[1 + k / 2] >> ((k & 1) ? 0 : 4)) & 15;
						if (v >= 8)
							v -= 16;
						int x = (v * sc * 2048 + 1024 + coef[2 * ci] * h1 + coef[2 * ci + 1] * h2) >> 11;
						x = x > 32767 ? 32767 : x < -32768 ? -32768 : x;
						h2 = h1;
						h1 = x;
						pcm[(f * 14 + k) * ch + c] = (s16)x;
					}
				}
			}
			size_t wn = 0;
			u8 *w = nds_strm_make_wav (pcm, ns < fr * 14 ? ns : (uint)(fr * 14), ch, rate, &wn);
			err = w ? shade_write (arg, dest, basedir, name, w, (uint)wn) : ERR_OUT_OF_MEMORY;
			FREE (w);
		}
	}
done:
	FREE (pcm);
	FREE (raw);
	return err;
}
