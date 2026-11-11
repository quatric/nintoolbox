// Nintendo DS streams (SDK "STRM", ".strm") and single waves (SDK "SWAV", ".swav"): decoded to "<name>.wav" (16 bit PCM) beside
// the source. Layout after vgmstream's nds_strm.c / vgmtrans NDSInstrSet: STRM = "STRM" BOM 0xFFFE0001, "HEAD" at 0x10 with
// codec (0 PCM8, 1 PCM16, 2 IMA-ADPCM) 0x18, loop flag 0x19, channels 0x1a, rate u16 0x1c, loop start 0x20, samples 0x24,
// data offset 0x28, block size 0x30, last block size 0x38 (all LE); channels are interleaved per block and every IMA block
// starts with a {s16 predictor, u16 step index} header. SWAV = "SWAV", "DATA" at 0x10, wave type 0x18 (same codes), loop 0x19,
// rate u16 0x1a, loop offset u16 0x1e (words), non-loop length u32 0x20 (words), data at 0x24 (IMA data starts with its header).
static const u16 nds_strm_step[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
	107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282,
	1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
	12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };

// decodes one IMA block (block header + nibbles) into out[]; returns the number of samples written (at most max)
static uint nds_strm_ima_block (const u8 *p, uint size, s16 *out, uint max)
{
	if (size < 4)
		return 0;
	int pcm = (s16)(p[0] | p[1] << 8), idx = p[2] & 127;
	if (idx > 88)
		idx = 88;
	uint n = 0;
	for (uint i = 0; i < (size - 4) * 2 && n < max; i++)
	{
		const uint nib = i & 1 ? p[4 + i / 2] >> 4 : p[4 + i / 2] & 15;
		const int step = nds_strm_step[idx];
		int diff = step >> 3;
		if (nib & 1) diff += step >> 2;
		if (nib & 2) diff += step >> 1;
		if (nib & 4) diff += step;
		pcm += nib & 8 ? -diff : diff;
		pcm = pcm < -32768 ? -32768 : pcm > 32767 ? 32767 : pcm;
		static const int adj[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
		idx += adj[nib & 7];
		idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
		out[n++] = (s16)pcm;
	}
	return n;
}

static u8 *nds_strm_make_wav (const s16 *pcm, uint frames, uint ch, uint rate, size_t *out_size)
{
	const size_t bytes = (size_t)frames * ch * 2;
	u8 *w = MALLOC (44 + bytes);
	if (!w)
		return 0;
	memcpy (w, "RIFF", 4);
	#define W32(o, v) do { w[o] = (v); w[o + 1] = (v) >> 8; w[o + 2] = (v) >> 16; w[o + 3] = (v) >> 24; } while (0)
	W32 (4, (u32)(36 + bytes));
	memcpy (w + 8, "WAVEfmt ", 8);
	W32 (16, 16);
	w[20] = 1; w[21] = 0; w[22] = ch; w[23] = 0;
	W32 (24, rate);
	W32 (28, rate * ch * 2);
	w[32] = ch * 2; w[33] = 0; w[34] = 16; w[35] = 0;
	memcpy (w + 36, "data", 4);
	W32 (40, (u32)bytes);
	#undef W32
	for (size_t i = 0; i < (size_t)frames * ch; i++)
	{
		w[44 + 2 * i] = (u16)pcm[i];
		w[45 + 2 * i] = (u16)pcm[i] >> 8;
	}
	*out_size = 44 + bytes;
	return w;
}

static enumError extract_nds_strm_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if ((!is_ext (arg, ".strm") && !is_ext (arg, ".swav")) || (export_count == 0 && !opt_decode))
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
	uint frames = 0, ch = 0, rate = 0;
	if (sz > 0x40 && !memcmp (raw, "STRM", 4) && !memcmp (raw + 0x10, "HEAD", 4))
	{
		const uint codec = raw[0x18];
		ch = raw[0x1a];
		rate = raw[0x1c] | raw[0x1d] << 8;
		const uint ns = rd_le32 (raw + 0x24), start = rd_le32 (raw + 0x28), blk = rd_le32 (raw + 0x30), lastblk = rd_le32 (raw + 0x38);
		if (codec <= 2 && ch >= 1 && ch <= 2 && rate && ns && ns < 0x4000000 && blk && start < sz && blk < 0x100000)
		{
			frames = ns;
			pcm = CALLOC ((size_t)frames * ch, sizeof (s16));
			if (pcm)
			{
				const size_t per_blk = codec == 2 ? (blk - 4) * 2 : codec == 1 ? blk / 2 : blk;
				for (uint c = 0; c < ch; c++)
				{
					uint done = 0;
					for (size_t b = 0; done < frames; b++)
					{
						const size_t off = start + (b * ch + c) * blk;
						if (off >= sz)
							break;
						uint bs = blk;
						if (off + bs > sz)
							bs = sz - off;
						(void)lastblk;
						const uint want = frames - done < per_blk ? frames - done : (uint)per_blk;
						uint n = 0;
						if (codec == 2)
						{
							s16 *t = MALLOC (per_blk * sizeof (s16) + 16);
							if (!t) break;
							n = nds_strm_ima_block (raw + off, bs, t, want);
							for (uint i = 0; i < n; i++)
								pcm[(size_t)(done + i) * ch + c] = t[i];
							FREE (t);
						}
						else
						{
							for (n = 0; n < want && (codec == 1 ? 2 * n + 1 : n) < bs; n++)
								pcm[(size_t)(done + n) * ch + c] = codec == 1 ? (s16)(raw[off + 2 * n] | raw[off + 2 * n + 1] << 8) : (s16)((s8)raw[off + n] << 8);
						}
						if (!n)
							break;
						done += n;
					}
				}
			}
		}
	}
	else if (sz > 0x24 && !memcmp (raw, "SWAV", 4) && !memcmp (raw + 0x10, "DATA", 4))
	{
		const uint codec = raw[0x18];
		rate = raw[0x1a] | raw[0x1b] << 8;
		const uint words = (raw[0x1e] | raw[0x1f] << 8) + rd_le32 (raw + 0x20);
		size_t bytes = (size_t)words * 4;
		if (0x24 + bytes > sz)
			bytes = sz - 0x24;
		ch = 1;
		if (codec <= 2 && rate && bytes)
		{
			frames = codec == 2 ? (uint)((bytes - 4) * 2) : codec == 1 ? (uint)(bytes / 2) : (uint)bytes;
			pcm = CALLOC ((size_t)frames + 1, sizeof (s16));
			if (pcm)
			{
				if (codec == 2)
					frames = nds_strm_ima_block (raw + 0x24, (uint)bytes, pcm, frames);
				else
					for (uint i = 0; i < frames; i++)
						pcm[i] = codec == 1 ? (s16)(raw[0x24 + 2 * i] | raw[0x25 + 2 * i] << 8) : (s16)((s8)raw[0x24 + i] << 8);
			}
		}
	}
	if (pcm && frames)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot) strcpy (dot, ".wav");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE NDS-STRM/SWAV:%s (%u ch, %u Hz, %u samples) -> WAV\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, frames);
		if (testmode)
			err = ERR_OK;
		else
		{
			size_t wn = 0;
			u8 *w = nds_strm_make_wav (pcm, frames, ch, rate, &wn);
			err = w ? shade_write (arg, dest, basedir, name, w, (uint)wn) : ERR_OUT_OF_MEMORY;
			FREE (w);
		}
	}
	FREE (pcm);
	FREE (raw);
	return err;
}
