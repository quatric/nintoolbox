// Electronic Arts "SCHl" streams whose audio is EA-XA ADPCM (the Wii/GameCube "split mono" EAXA codec, e.g. the
// ".exa" streams of The Godfather: Blackhand Edition), decoded natively to "<file>.wav" in decode mode because
// ffmpeg mistakes them for EA R2. Layout after vgmstream's ea_schl / blocked_ea_schl / ea_xa_decoder:
//   "SCHl", u32 header size (LE), "PT" + u16 platform (6 = GC/Wii) and variable patches {id, byte count, value}
//   (0x80 version, 0x82 channels, 0x83 / 0xa0 codec, 0x84 rate, 0x85 samples, 0xff end), then blocks
//   {id, u32 size (LE), ...}; "SCDl" blocks hold {u32 samples (BE), u32 channel offset * channels (BE), data}.
//   Version 3 (and 2 on generic platforms) uses the revised frames of 0x0f bytes (28 samples, byte 0xee starts
//   an uncompressed frame of 2 history and 28 big-endian samples); older versions have the original decoder
//   with rounding and a 4-byte history in front of each channel. Channels are split, so they are interleaved here.
typedef struct eaxa_hdr_t
{
	int platform, version, codec1, codec2;
	uint channels, rate, samples;
} eaxa_hdr_t;

static uint eaxa_patch (const u8 *p, size_t n, size_t *o)
{
	if (*o >= n)
		return 0;
	uint cnt = p[(*o)++], v = 0;
	if (cnt == 0xff)
	{
		if (*o + 4 > n)
			return 0;
		*o += 4 + rd_be32 (p + *o);
		return 0;
	}
	if (cnt > 4)
	{
		*o += cnt;
		return 0;
	}
	for (; cnt && *o < n; cnt--)
		v = (v << 8) | p[(*o)++];
	return v;
}

static bool eaxa_parse (const u8 *p, size_t n, eaxa_hdr_t *h)
{
	memset (h, 0, sizeof (*h));
	h->version = h->codec1 = h->codec2 = -1;
	if (n < 12 || memcmp (p + 8, "PT", 2))
		return false;
	h->platform = p[10] | (p[11] << 8);
	size_t o = 12;
	bool end = false;
	while (!end && o < n)
	{
		const uint t = p[o++];
		switch (t)
		{
		case 0x00: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0a: case 0x0b:
		case 0x0c: case 0x0d: case 0x0e: case 0x0f: case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15:
		case 0x19: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f: case 0x20: case 0x21: case 0x22: case 0x23:
		case 0x24: case 0x25: case 0x1a: case 0x26: case 0x27: case 0x28: case 0x29: case 0x2a: case 0x81: case 0x86:
		case 0x87: case 0x88: case 0x89: case 0x94: case 0x95: case 0xa2: case 0xa3: case 0x8f: case 0x90: case 0x91:
		case 0xab: case 0xac: case 0xad: case 0x8c: case 0x8a: case 0x8b: case 0x8d: case 0x8e: case 0x92: case 0x93:
		case 0x98: case 0x99: case 0x9c: case 0x9d: case 0x9e: case 0x9f: case 0xa6: case 0xa7: case 0xa1:
			eaxa_patch (p, n, &o);
			break;
		case 0xfc: case 0xfd: break;
		case 0x80: h->version = (int)eaxa_patch (p, n, &o); break;
		case 0x83: h->codec1 = (int)eaxa_patch (p, n, &o); break;
		case 0xa0: h->codec2 = (int)eaxa_patch (p, n, &o); break;
		case 0x82: h->channels = eaxa_patch (p, n, &o); break;
		case 0x84: h->rate = eaxa_patch (p, n, &o); break;
		case 0x85: h->samples = eaxa_patch (p, n, &o); break;
		case 0xff: case 0xfe: end = true; break;
		default: return false;
		}
	}
	if (!h->channels)
		h->channels = 1;
	if (h->version < 0)
		h->version = h->platform == 6 ? 2 : h->platform == 8 ? 2 : h->platform >= 0x10 || h->platform == 9 ? 3 : -1;
	if (h->codec2 < 0 && h->codec1 == 7)
		h->codec2 = (h->platform == 0 || h->platform == 3) ? 3 : 0x0a;
	if (!h->rate)
		h->rate = h->platform == 6 ? 24000 : h->platform == 0x10 ? 32000 : 0;
	return h->version >= 0 && h->rate;
}

static const int eaxa_tab[20] = {0, 240, 460, 392, 0, 0, -208, -220, 0, 1, 3, 4, 7, 8, 10, 11, 0, -1, -3, -4};

static enumError extract_eaxa_file (ccp arg)
{
	if (export_count <= 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	eaxa_hdr_t h;
	const size_t hsz = sz >= 12 ? rd_le32 (raw + 4) : 0;
	if (sz < 16 || memcmp (raw, "SCHl", 4) || hsz < 12 || hsz > sz || !eaxa_parse (raw, hsz, &h) || h.codec2 != 0x0a
		|| h.channels > 8 || h.rate < 4000 || h.rate > 96000 || (h.platform != 6 && h.platform != 8 && h.platform != 0x10))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const bool pcm_blocks = h.version == 3 || (h.version == 2 && h.platform == 8);
	const uint ch = h.channels;
	int hist1[8] = {0}, hist2[8] = {0};
	size_t cap = (size_t)(h.samples ? h.samples : 1 << 20) + 4096, n = 0;
	s16 *pcm = MALLOC (cap * ch * sizeof (s16));
	bool ok = pcm != 0;
	for (size_t b = hsz; ok && b + 12 <= sz;)
	{
		size_t bs = rd_le32 (raw + b + 4);
		if (bs > sz - b || bs < 8)
			bs = rd_be32 (raw + b + 4);
		if (!memcmp (raw + b, "SCEl", 4) || !rd_be32 (raw + b) || bs < 8 || bs > sz - b)
			break;
		if (!memcmp (raw + b, "SCDl", 4) && bs >= 12 + 4 * (size_t)ch)
		{
			const uint ns = rd_be32 (raw + b + 8);
			if (ns > 0x100000)
			{
				ok = false;
				break;
			}
			if (n + ns > cap)
			{
				cap = (n + ns) * 2;
				pcm = REALLOC (pcm, cap * ch * sizeof (s16));
				if (!pcm)
				{
					ok = false;
					break;
				}
			}
			for (uint c = 0; c < ch && ok; c++)
			{
				size_t o = b + 12 + 4 * (size_t)ch + rd_be32 (raw + b + 12 + 4 * (size_t)c);
				if (!pcm_blocks)
					o += 4;
				uint done = 0;
				while (done < ns)
				{
					const uint cnt = ns - done < 28 ? ns - done : 28;
					if (o + 1 > b + bs)
					{
						ok = false;
						break;
					}
					const uint fi = raw[o];
					if (pcm_blocks && fi == 0xee)
					{
						if (o + 0x3d > b + bs)
						{
							ok = false;
							break;
						}
						hist1[c] = (s16)rd_be16 (raw + o + 1);
						hist2[c] = (s16)rd_be16 (raw + o + 3);
						for (uint i = 0; i < cnt; i++)
							pcm[(n + done + i) * ch + c] = (s16)rd_be16 (raw + o + 5 + 2 * i);
						o += 0x3d;
					}
					else
					{
						if (o + 0x0f > b + bs)
						{
							ok = false;
							break;
						}
						const int c1 = eaxa_tab[fi >> 4], c2 = eaxa_tab[(fi >> 4) + 4], sh = (int)(fi & 15) + 8;
						for (uint i = 0; i < cnt; i++)
						{
							const uint by = raw[o + 1 + i / 2], nb = (i & 1) ? by & 15 : by >> 4;
							int v = (int)((u32)nb << 28) >> sh;
							v = v + c1 * hist1[c] + c2 * hist2[c];
							v = (v + (pcm_blocks ? 0 : 128)) >> 8;
							v = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
							pcm[(n + done + i) * ch + c] = (s16)v;
							hist2[c] = hist1[c];
							hist1[c] = v;
						}
						o += 0x0f;
					}
					done += cnt;
				}
			}
			n += ns;
		}
		b += bs;
	}
	FREE (raw);
	if (!ok || !n)
	{
		FREE (pcm);
		return ERR_NOTHING_TO_DO;
	}
	const size_t dsz = n * ch * 2;
	u8 *wav = MALLOC (44 + dsz);
	if (!wav)
	{
		FREE (pcm);
		return ERR_OUT_OF_MEMORY;
	}
	memcpy (wav, "RIFF", 4);
	wr_le32 (wav + 4, (u32)(36 + dsz));
	memcpy (wav + 8, "WAVEfmt ", 8);
	wr_le32 (wav + 16, 16);
	wr_le16 (wav + 20, 1);
	wr_le16 (wav + 22, (u16)ch);
	wr_le32 (wav + 24, h.rate);
	wr_le32 (wav + 28, h.rate * ch * 2);
	wr_le16 (wav + 32, (u16)(ch * 2));
	wr_le16 (wav + 34, 16);
	memcpy (wav + 36, "data", 4);
	wr_le32 (wav + 40, (u32)dsz);
	for (size_t i = 0; i < n * ch; i++)
		wr_le16 (wav + 44 + 2 * i, (u16)pcm[i]);
	FREE (pcm);
	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s.wav", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE EA-XA:%s (%u ch, %u Hz, %zu samples) -> WAV:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, ch, h.rate, n, dest);
	enumError err = ERR_OK;
	if (!testmode)
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f && fwrite (wav, 1, (uint)(44 + dsz), F.f) != (uint)(44 + dsz))
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing failed: %s\n", dest);
			ResetFile (&F, opt_preserve);
		}
	FREE (wav);
	return err;
}
