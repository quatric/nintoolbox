// Procyon Studio Digital Sound Elements DS audio (".sad": "sadl", Professor Layton, Soma Bringer, Ni no Kuni): loop flag @0x31, channels @0x32, flags @0x33
// (rate bits 6: 4 -> 32728 Hz, else 16364 Hz; codec = flags & 0xf0: 0x00/0x70 IMA with per-channel {s16 predictor, s16 step} at 0x80, 0xb0 Procyon ADPCM),
// data size @0x40, data start @0x48; channels interleave every 0x10 bytes. Decoded to "<name>.wav" (after vgmstream sadl.c / nds_procyon_decoder.c).
static const int8_t sadl_proc_coef[5][2] = { { 0, 0 }, { 60, 0 }, { 115, -52 }, { 98, -55 }, { 122, -60 } };

static enumError extract_nds_sadl_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".sad") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 0x100 && !memcmp (raw, "sadl", 4))
	{
		const uint ch = raw[0x32], flags = raw[0x33], codec = flags & 0xf0, rate = (flags & 6) == 4 ? 32728 : (flags & 6) <= 2 ? 16364 : 0;
		const size_t start = rd_le32 (raw + 0x48);
		size_t dsz = rd_le32 (raw + 0x40);
		dsz = dsz > start ? dsz - start : 0;
		if (start + dsz > sz)
			dsz = start < sz ? sz - start : 0;
		if (ch >= 1 && ch <= 2 && rate && dsz >= 0x10 * ch && (codec == 0x00 || codec == 0x70 || codec == 0xb0) && start >= 0x88)
		{
			const size_t frames_per_ch = dsz / 0x10 / ch;
			uint n = codec == 0xb0 ? (uint)(frames_per_ch * 30) : (uint)(frames_per_ch * 32);
			s16 *pcm = CALLOC ((size_t)n * ch + 1, sizeof (s16));
			if (pcm)
			{
				for (uint c = 0; c < ch; c++)
				{
					uint pos = 0;
					if (codec == 0xb0)
					{
						int h1 = 0, h2 = 0;
						for (size_t f = 0; f < frames_per_ch; f++)
						{
							const u8 *p = raw + start + (f * ch + c) * 0x10;
							const uint hd = p[0xf] ^ 0x80;
							const int scale = 12 - (int)(hd & 15), idx = (hd >> 4) & 15;
							const int c1 = idx < 5 ? sadl_proc_coef[idx][0] : 0, c2 = idx < 5 ? sadl_proc_coef[idx][1] : 0;
							for (uint i = 0; i < 30; i++)
							{
								const uint b = p[i / 2] ^ 0x80;
								int nb = i & 1 ? b >> 4 : b & 15;
								nb = nb >= 8 ? nb - 16 : nb;
								int s = nb * 64 * 64;
								s = scale < 0 ? s << -scale : s >> scale;
								s = (h1 * c1 + h2 * c2 + 32) / 64 + s * 64;
								h2 = h1;
								h1 = s;
								int o = (s + 32) / 64;
								o = o < -32768 ? -32768 : o > 32767 ? 32767 : o;
								pcm[(size_t)pos++ * ch + c] = (s16)(o / 64 * 64);
							}
						}
					}
					else
					{
						u8 *buf = MALLOC (frames_per_ch * 0x10 + 4);
						if (!buf)
							break;
						memcpy (buf, raw + 0x80 + c * 4, 4);
						for (size_t f = 0; f < frames_per_ch; f++)
							memcpy (buf + 4 + f * 0x10, raw + start + (f * ch + c) * 0x10, 0x10);
						s16 *t = CALLOC (n + 1, sizeof (s16));
						if (t)
						{
							const uint got = nds_strm_ima_block (buf, (uint)(frames_per_ch * 0x10 + 4), t, n);
							for (uint i = 0; i < got; i++)
								pcm[(size_t)i * ch + c] = t[i];
						}
						FREE (t);
						FREE (buf);
					}
				}
				char dest[PATH_MAX], name[PATH_MAX];
				beside_source_dest (dest, sizeof (dest), arg);
				const char *sl = strrchr (arg, '/');
				snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
				char *dot = strrchr (name, '.');
				if (dot) strcpy (dot, ".wav");
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sDECODE NDS-SADL:%s (%u ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, n);
				if (testmode)
					err = ERR_OK;
				else
				{
					size_t wn = 0;
					u8 *w = nds_strm_make_wav (pcm, n, ch, rate, &wn);
					err = w ? shade_write (arg, dest, basedir, name, w, (uint)wn) : ERR_OUT_OF_MEMORY;
					FREE (w);
				}
				FREE (pcm);
			}
		}
	}
	FREE (raw);
	return err;
}
