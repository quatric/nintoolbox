// EA Sports Grand Slam Tennis (Wii) "data/rev/audio/**/*.sbr" + sibling "*.sbs" sound banks ("SBKR" / "SBKS", EA audio core). The big-endian ".sbr" has {"SBKR", ..., u32 count @0x1c, ...}, tag list at 0xb0 and from 0xbc
// count * 10-byte entries {u32 sound id, u16 2, u32 record ptr}; a record is {u32 0, u16 ptr to the stream header, u16 1, u32 ptr to the stream offset, u32 0}. The stream header is the EAAC header {u32: version:4,
// codec:4 (4 = XAS v1), channel config:6, sample rate:18; u32: type:2, loop:1, samples:29}; the stream offset (u32) addresses the ".sbs" (read per stream, up to ~100 MB). The ".sbs" holds EA SNS blocks
// {u32 flags:8 | size:24, u32 samples, per-channel runs of 0x4c-byte EA-XAS v1 frames}: 4 groups per frame, each with a LE header word {coef idx:4, hist2:12, shift:4, hist1:12} and 15 rows of byte-interleaved nibbles
// (128 samples per frame; coefficient pairs 0, 15/16, 115/64 & -13/16, 49/32 & -55/64). Sounds are decoded to "<file>.d/<id>.wav" (16-bit PCM).
static enumError extract_eabank_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".sbr"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	FILE *sbs = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0xd0 || memcmp (raw, "SBKR", 4) || rd_be32 (raw + 0x24) != 0xbc)
		goto done;
	{
		const u32 count = rd_be32 (raw + 0x1c);
		if (!count || count > 100000 || 0xbc + (u64)count * 10 > sz)
			goto done;
		char sp[PATH_MAX];
		snprintf (sp, sizeof (sp), "%s", arg);
		char *dot = strrchr (sp, '.');
		if (!dot)
			goto done;
		strcpy (dot, ".sbs");
		sbs = fopen (sp, "rb");
		struct stat st;
		if (!sbs || fstat (fileno (sbs), &st) || st.st_size < 0x20)
			goto done;
		const u64 fsz = (u64)st.st_size;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SBR:%s (%u sounds) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, count, dest);
		err = ERR_OK;
		static const double coef[4][2] = { { 0, 0 }, { 0.9375, 0 }, { 1.796875, -0.8125 }, { 1.53125, -0.859375 } };
		for (uint i = 0; i < count && !err && !testmode; i++)
		{
			const u8 *e = raw + 0xbc + (size_t)i * 10;
			const u32 id = rd_be32 (e), rp = rd_be32 (e + 6);
			if (rp + 12 > sz)
				continue;
			const uint snr = rd_be16 (raw + rp + 4), sns = rd_be32 (raw + rp + 8);
			if (snr + 8 > sz || sns + 4 > sz)
				continue;
			const u32 h1 = rd_be32 (raw + snr), h2 = rd_be32 (raw + snr + 4), off = rd_be32 (raw + sns);
			const uint codec = (h1 >> 24) & 15, nch = ((h1 >> 18) & 63) + 1, rate = h1 & 0x3ffff;
			const u32 nsmp = h2 & 0x1fffffff;
			if (codec != 4 || nch > 2 || rate < 4000 || rate > 96000 || !nsmp || nsmp > 0x4000000 || off >= fsz)
				continue;
			s16 *pcm = (s16 *)CALLOC (nsmp * nch + 1, sizeof (s16));
			if (!pcm)
				continue;
			u64 pos = off;
			u32 done_smp = 0;
			while (done_smp < nsmp && pos + 8 <= fsz)
			{
				u8 bh[8];
				if (fseeko (sbs, (off_t)pos, SEEK_SET) || fread (bh, 1, 8, sbs) != 8)
					break;
				const u32 flags = bh[0], bsz = rd_be32 (bh) & 0xffffff, bs = rd_be32 (bh + 4);
				if (bsz < 8 || pos + bsz > fsz)
					break;
				if (flags == 0 || flags == 0x80)
				{
					const uint frames = (bs + 127) / 128;
					if (!bs || (size_t)frames * 0x4c * nch > bsz - 8)
						break;
					u8 *blk = MALLOC (bsz - 8);
					if (!blk || fread (blk, 1, bsz - 8, sbs) != bsz - 8)
					{
						FREE (blk);
						break;
					}
					const uint take = bs < nsmp - done_smp ? bs : nsmp - done_smp;
					for (uint c = 0; c < nch; c++)
						for (uint f = 0; f < frames; f++)
						{
							const u8 *fr = blk + ((size_t)c * frames + f) * 0x4c;
							for (uint g = 0; g < 4; g++)
							{
								const u32 H = rd_le32 (fr + g * 4);
								const double c1 = coef[(H & 15) < 4 ? H & 15 : 0][0], c2 = coef[(H & 15) < 4 ? H & 15 : 0][1];
								const uint shift = (H >> 16) & 15;
								int h2s = (s16)(H & 0xfff0), h1s = (s16)((H >> 16) & 0xfff0);
								const uint base = f * 128 + g * 32;
								#define XAS_PUT(v) do { if (base + k < take) pcm[(size_t)(done_smp + base + k) * nch + c] = (s16)(v); } while (0)
								uint k = 0;
								XAS_PUT (h2s); k++;
								XAS_PUT (h1s); k++;
								for (uint r = 0; r < 15; r++)
								{
									const uint by = fr[0x10 + r * 4 + g];
									for (uint nb = 0; nb < 2; nb++, k++)
									{
										const uint n = nb ? by & 15 : by >> 4;
										const int s = ((s16)(n << 12)) >> shift;
										int v = (int)(s + h1s * c1 + h2s * c2);
										v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
										XAS_PUT (v);
										h2s = h1s;
										h1s = v;
									}
								}
								#undef XAS_PUT
							}
						}
					FREE (blk);
					done_smp += take;
				}
				else if (fseeko (sbs, (off_t)(pos + bsz), SEEK_SET))
					break;
				if (flags == 0x80)
					break;
				pos += bsz;
			}
			if (done_smp)
			{
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%s%08x.wav", dest, basedir ? basedir : "", id);
				const size_t bytes = (size_t)done_smp * nch * 2;
				u8 h[44];
				memcpy (h, "RIFF", 4);
				wr_le32 (h + 4, (u32)(36 + bytes));
				memcpy (h + 8, "WAVEfmt ", 8);
				wr_le32 (h + 16, 16);
				h[20] = 1; h[21] = 0; h[22] = nch; h[23] = 0;
				wr_le32 (h + 24, rate);
				wr_le32 (h + 28, rate * nch * 2);
				h[32] = nch * 2; h[33] = 0; h[34] = 16; h[35] = 0;
				memcpy (h + 36, "data", 4);
				wr_le32 (h + 40, (u32)bytes);
				u8 *le = MALLOC (bytes);
				if (le)
				{
					for (size_t q = 0; q < (size_t)done_smp * nch; q++)
					{
						le[q * 2] = (u8)pcm[q];
						le[q * 2 + 1] = (u8)((u16)pcm[q] >> 8);
					}
					File_t F;
					err = CreateFileOpt (&F, true, path, false, arg);
					if (F.f && (fwrite (h, 1, sizeof (h), F.f) != sizeof (h) || fwrite (le, 1, bytes, F.f) != bytes))
						err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", bytes, path);
					ResetFile (&F, opt_preserve);
					FREE (le);
				}
			}
			FREE (pcm);
		}
	}
done:
	if (sbs)
		fclose (sbs);
	FREE (raw);
	(void)depth;
	return err;
}
