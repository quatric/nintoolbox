// Nintendo Switch BWAV ("BWAV", ".bwav"): decoded natively to "<name>.wav" (PCM16 and DSP-ADPCM, any channel count, loop points
// kept in a 'smpl' chunk). Layout after vgmstream's bwav.c: "BWAV" BOM(0xFEFF) version crc prefetch u16 channels u16 (0x0e), then
// one 0x4c byte record per channel from 0x10: codec u16 (0 PCM16, 1 DSP; 2 = Opus is left alone), pan u16, rate u32, total samples
// u32, samples u32 (0x0c), 16 s16 DSP coefficients (0x10), start offset u32 (0x34), loop end s32 (0x3c, -1 = none), loop start
// s32 (0x40), DSP history s16 x2 (0x46). Every channel's data is its own contiguous block starting at its start offset.
static enumError extract_bwav_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".bwav") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	s16 *pcm = 0;
	if (sz > 0x10 + 0x4c && !memcmp (raw, "BWAV", 4) && raw[4] == 0xff && raw[5] == 0xfe)
	{
		const uint ch = raw[0x0e] | raw[0x0f] << 8;
		if (ch >= 1 && ch <= 8 && sz >= 0x10 + 0x4c * (size_t)ch)
		{
			const u8 *c0 = raw + 0x10;
			const uint codec = c0[0] | c0[1] << 8, rate = rd_le32 (c0 + 4), ns = rd_le32 (c0 + 0x0c);
			const s32 loop_end = (s32)rd_le32 (c0 + 0x3c), loop_start = (s32)rd_le32 (c0 + 0x40);
			bool ok = codec <= 1 && rate && ns && ns < 0x4000000;
			for (uint c = 1; ok && c < ch; c++)
			{
				const u8 *r = raw + 0x10 + 0x4c * c;
				ok = (uint)(r[0] | r[1] << 8) == codec;
			}
			pcm = ok ? CALLOC ((size_t)ns * ch, sizeof (s16)) : 0;
			for (uint c = 0; pcm && c < ch; c++)
			{
				const u8 *r = raw + 0x10 + 0x4c * c;
				const size_t start = rd_le32 (r + 0x34);
				s16 coef[16];
				for (uint k = 0; k < 16; k++)
					coef[k] = (s16)(r[0x10 + 2 * k] | r[0x11 + 2 * k] << 8);
				int h1 = (s16)(r[0x46] | r[0x47] << 8), h2 = (s16)(r[0x48] | r[0x49] << 8);
				for (uint k = 0; k < ns; k++)
				{
					int v;
					if (codec == 0)
					{
						const size_t o = start + 2 * (size_t)k;
						v = o + 1 < sz ? (s16)(raw[o] | raw[o + 1] << 8) : 0;
					}
					else
					{
						const size_t fp = start + (k / 14) * 8;
						const uint hd = fp < sz ? raw[fp] : 0, ix = k % 14;
						const size_t bp = fp + 1 + ix / 2;
						const uint x = bp < sz ? raw[bp] : 0;
						int q = ix & 1 ? x & 15 : x >> 4;
						if (q >= 8)
							q -= 16;
						const uint pr = (hd >> 4) & 7;
						v = (int)((((s64)q << (hd & 15)) << 11) + 0x400 + (s64)coef[2 * pr] * h1 + (s64)coef[2 * pr + 1] * h2) >> 11;
						v = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
						h2 = h1;
						h1 = v;
					}
					pcm[(size_t)k * ch + c] = (s16)v;
				}
			}
			if (pcm)
			{
				const bool loop = loop_end != -1 && loop_start >= 0 && (u32)loop_end <= ns;
				const size_t bytes = (size_t)ns * ch * 2, extra = loop ? 68 : 0;
				u8 *w = MALLOC (44 + bytes + extra);
				if (w)
				{
#define W32(o, v) do { w[o] = (v); w[(o) + 1] = (v) >> 8; w[(o) + 2] = (v) >> 16; w[(o) + 3] = (v) >> 24; } while (0)
					memcpy (w, "RIFF", 4);
					W32 (4, (u32)(36 + bytes + extra));
					memcpy (w + 8, "WAVEfmt ", 8);
					W32 (16, 16);
					w[20] = 1; w[21] = 0; w[22] = ch; w[23] = 0;
					W32 (24, rate);
					W32 (28, rate * ch * 2);
					w[32] = ch * 2; w[33] = 0; w[34] = 16; w[35] = 0;
					memcpy (w + 36, "data", 4);
					W32 (40, (u32)bytes);
					for (size_t i = 0; i < (size_t)ns * ch; i++)
						w[44 + 2 * i] = (u16)pcm[i], w[45 + 2 * i] = (u16)pcm[i] >> 8;
					if (loop)
					{
						u8 *s = w + 44 + bytes;
						memset (s, 0, 68);
						memcpy (s, "smpl", 4);
						W32 (4 + (s - w), 60);
						// 9 header words (manufacturer, product, period, unity note, fraction, smpte fmt, offset, loops, extra)
						W32 (8 + 8 + (s - w), 1000000000u / rate);
						W32 (8 + 12 + (s - w), 60);
						W32 (8 + 28 + (s - w), 1);
						// loop: id, type, start, end, fraction, count
						W32 (8 + 36 + 8 + (s - w), (u32)loop_start);
						W32 (8 + 36 + 12 + (s - w), (u32)loop_end - 1);
					}
#undef W32
					char dest[PATH_MAX], name[PATH_MAX];
					beside_source_dest (dest, sizeof (dest), arg);
					const char *sl = strrchr (arg, '/');
					snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
					char *dot = strrchr (name, '.');
					if (dot) strcpy (dot, ".wav");
					if (verbose >= 0 || testmode)
						fprintf (stdlog, "%s%sDECODE BWAV:%s (%u ch, %u Hz, %u samples, %s) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, ns, codec ? "DSP-ADPCM" : "PCM16");
					err = testmode ? ERR_OK : shade_write (arg, dest, basedir, name, w, (uint)(44 + bytes + extra));
					FREE (w);
				}
			}
		}
	}
	FREE (pcm);
	FREE (raw);
	return err;
}
