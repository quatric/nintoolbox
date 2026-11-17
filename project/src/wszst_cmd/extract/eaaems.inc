// EA Sports "AEMS" streams (FaceBreaker K.O. Party "data/wii/audio/aems/*.aems", header "S10S", packets from 0x40): a chain of packets {u32 flags | size (the size covers the packet),
// u32 samples per channel, payload}; flag 0x80 packets are stream-control records and skipped. The payload is EA-XAS v1 (EAAC codec 4): 0x4c-byte blocks of 128 samples whose four
// 4-byte little-endian group headers {bits 0-3 coefficient index (0-3), bits 0-15 & 0xfff0 = hist2, bits 16-31 & 0xfff0 = hist1, bits 16-19 shift} are followed by 15 rows of 4 data bytes (one
// byte per group, high nibble first); blocks of the channels alternate. The sample rate is not stored in the file; 32000 Hz (the rate of the game's other EAAC streams) is assumed. The result is
// written to "<file>.wav". (Layout per vgmstream's EA-XAS v1 decoder.)
static void eaxas_block (const u8 *b, short *out, uint stride)
{
	static const double k0[4] = { 0, 0.9375, 1.796875, 1.53125 }, k1[4] = { 0, 0, -0.8125, -0.859375 };
	for (uint g = 0; g < 4; g++)
	{
		const u32 h = rd_le32 (b + 4 * g);
		const uint ci = h & 15;
		const double c0 = ci < 4 ? k0[ci] : 0, c1 = ci < 4 ? k1[ci] : 0;
		int h2 = (short)(h & 0xfff0), h1 = (short)((h >> 16) & 0xfff0);
		const uint shift = (h >> 16) & 15;
		uint n = 0;
		out[(g * 32 + n++) * stride] = (short)h2;
		out[(g * 32 + n++) * stride] = (short)h1;
		for (uint r = 0; r < 15; r++)
		{
			const uint byte = b[16 + r * 4 + g];
			for (uint half = 0; half < 2; half++)
			{
				const uint nib = half ? byte & 15 : byte >> 4;
				int s = (int)((double)(((short)(nib << 12)) >> shift) + h1 * c0 + h2 * c1);
				s = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
				out[(g * 32 + n++) * stride] = (short)s;
				h2 = h1;
				h1 = s;
			}
		}
	}
}

static void eaxas_wav_header (u8 *hd, uint channels, uint rate, u32 dbytes)
{
	memcpy (hd, "RIFF", 4);
	wr_le32 (hd + 4, 36 + dbytes);
	memcpy (hd + 8, "WAVEfmt ", 8);
	wr_le32 (hd + 16, 16);
	hd[20] = 1; hd[21] = 0; hd[22] = channels; hd[23] = 0;
	wr_le32 (hd + 24, rate);
	wr_le32 (hd + 28, rate * channels * 2);
	hd[32] = channels * 2; hd[33] = 0; hd[34] = 16; hd[35] = 0;
	memcpy (hd + 36, "data", 4);
	wr_le32 (hd + 40, dbytes);
}

static enumError extract_eaaems_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".aems"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x80 || memcmp (raw, "S10S", 4))
		goto done;
	{
		const size_t start = 0x40;
		uint channels = 0;
		u64 total = 0;
		size_t o = start;
		bool ok = true;
		while (ok && o + 8 <= sz)
		{
			const u32 a = rd_be32 (raw + o), ns = rd_be32 (raw + o + 4), psz = a & 0xffffff;
			if (psz < 8 || o + psz > sz)
			{
				ok = false;
				break;
			}
			if ((!(a >> 24) && ns && !(ns % 128)) || ((a >> 24) && ns > 3))
			{
				const uint per = (ns + 127) / 128, nb = (psz - 8) / 76;
				if (!per || nb < per || nb / per > 2 || (channels && channels != nb / per))
				{
					ok = false;
					break;
				}
				channels = nb / per;
				total += ns;
			}
			o += psz;
		}
		if (!ok || o != sz || !channels || !total || total > (u64)1 << 31)
			goto done;
		char wav[PATH_MAX];
		snprintf (wav, sizeof (wav), "%s.wav", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT AEMS:%s (EA-XAS, %u channels, %llu samples) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, channels,
				(unsigned long long)total, wav);
		err = ERR_OK;
		if (testmode)
			goto done;
		File_t F;
		err = CreateFileOpt (&F, true, wav, false, arg);
		if (err || !F.f)
			goto done;
		const u32 rate = 32000, dbytes = (u32)(total * channels * 2);
		u8 hd[44];
		memcpy (hd, "RIFF", 4);
		wr_le32 (hd + 4, 36 + dbytes);
		memcpy (hd + 8, "WAVEfmt ", 8);
		wr_le32 (hd + 16, 16);
		hd[20] = 1; hd[21] = 0; hd[22] = channels; hd[23] = 0;
		wr_le32 (hd + 24, rate);
		wr_le32 (hd + 28, rate * channels * 2);
		hd[32] = channels * 2; hd[33] = 0; hd[34] = 16; hd[35] = 0;
		memcpy (hd + 36, "data", 4);
		wr_le32 (hd + 40, dbytes);
		if (fwrite (hd, 1, 44, F.f) != 44)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing failed: %s\n", wav);
		short *buf = MALLOC (sizeof (short) * 4 * 128 * 2 * 64);
		for (o = start; !err && o + 8 <= sz;)
		{
			const u32 a = rd_be32 (raw + o), ns = rd_be32 (raw + o + 4), psz = a & 0xffffff;
			if ((!(a >> 24) && ns) || ((a >> 24) && ns > 3))
			{
				const uint per = (ns + 127) / 128;
				short *pcm = MALLOC (sizeof (short) * per * 128 * channels);
				for (uint k = 0; k < per; k++)
					for (uint c = 0; c < channels; c++)
						eaxas_block (raw + o + 8 + 76 * (k * channels + c), pcm + k * 128 * channels + c, channels);
				if (fwrite (pcm, sizeof (short), (size_t)ns * channels, F.f) != (size_t)ns * channels)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing failed: %s\n", wav);
				FREE (pcm);
			}
			o += psz;
		}
		FREE (buf);
		ResetFile (&F, opt_preserve);
	}
done:
	FREE (raw);
	return err;
}

// FaceBreaker K.O. Party multi-track "audio/music/*.mus" (EAAC codec 4 = EA-XAS): a 0x2c-byte header, a table of 28-byte track records, one 16-byte EAAC stream header per track at 0x400 {u32 (bits
// 24-27 codec, bits 0-17 sample rate), u32 flags | samples, 0, 0}, then the tracks, each a run of the same {size, samples} packets as ".aems" starting on a 0x100 boundary (the first at 0x600) and
// ending with a flag-0x80 packet. Each track is written to "<file>.d/NNNN.wav".
static enumError extract_eamus_xas_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".mus"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	uint ntr = 0;
	if (sz < 0x800)
		goto done;
	while (0x400 + 16 * (ntr + 1) <= 0x600 && ((rd_be32 (raw + 0x400 + 16 * ntr) >> 24) & 0xf) == 4 && (rd_be32 (raw + 0x400 + 16 * ntr) >> 24) < 0x10)
		ntr++;
	if (!ntr)
		goto done;
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		size_t o = 0x400 + 16 * ntr;
		while (o + 8 <= sz && !rd_be32 (raw + o))
			o += 4;
		o &= ~(size_t)0xff;
		uint made = 0;
		err = ERR_OK;
		for (uint t = 0; t < ntr && o + 8 <= sz && !err; t++)
		{
			while (o + 8 <= sz && !rd_be32 (raw + o))
				o += 4;
			if (o + 8 > sz)
				break;
			const uint rate = rd_be32 (raw + 0x400 + 16 * t) & 0x3ffff;
			size_t p = o;
			u64 total = 0;
			uint channels = 0;
			bool ok = rate >= 4000 && rate <= 48000, fin = false;
			while (ok && !fin && p + 8 <= sz)
			{
				const u32 a = rd_be32 (raw + p), ns = rd_be32 (raw + p + 4), psz = a & 0xffffff;
				if (psz < 8 || p + psz > sz || !ns)
				{
					ok = false;
					break;
				}
				const uint per = (ns + 127) / 128, nb = (psz - 8) / 76;
				if (!per || nb < per || nb / per > 2 || (channels && channels != nb / per))
				{
					ok = false;
					break;
				}
				channels = nb / per;
				total += ns;
				fin = (a >> 24) != 0;
				p += psz;
			}
					if (!ok || !fin || !channels)
			{
				break;
			}
			if (!made && (verbose >= 0 || testmode))
				fprintf (stdlog, "%s%sEXTRACT MUS-XAS:%s (%u tracks) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ntr, dest);
			made++;
			if (!testmode)
			{
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%s%04u.wav", dest, basedir ? basedir : "", t);
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (!err && F.f)
				{
					u8 hd[44];
					eaxas_wav_header (hd, channels, rate, (u32)(total * channels * 2));
					if (fwrite (hd, 1, 44, F.f) != 44)
						err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing failed: %s\n", path);
					for (size_t q = o; !err && q < p;)
					{
						const u32 a = rd_be32 (raw + q), ns = rd_be32 (raw + q + 4), psz = a & 0xffffff;
						const uint per = (ns + 127) / 128;
						short *pcm = MALLOC (sizeof (short) * per * 128 * channels);
						for (uint k = 0; k < per; k++)
							for (uint c = 0; c < channels; c++)
								eaxas_block (raw + q + 8 + 76 * (k * channels + c), pcm + k * 128 * channels + c, channels);
						if (fwrite (pcm, sizeof (short), (size_t)ns * channels, F.f) != (size_t)ns * channels)
							err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing failed: %s\n", path);
						FREE (pcm);
						q += psz;
					}
				}
				ResetFile (&F, opt_preserve);
			}
			o = p;
		}
		if (!err)
			err = made ? ERR_OK : ERR_NOTHING_TO_DO;
	}
done:
	FREE (raw);
	return err;
}
