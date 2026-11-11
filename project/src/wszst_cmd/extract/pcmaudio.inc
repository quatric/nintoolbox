// Plain PCM audio containers converted to RIFF WAV ("<file>.d/<name>.wav"): AIFF / AIFF-C (".aif", ".aiff", ".aifc": big endian 8..32 bit PCM, "sowt" = little endian
// AIFF-C), Sun / NeXT ".au" (".snd" magic: 8..32 bit PCM and mu-law) and Creative Voice ".voc" (8 bit PCM data blocks, concatenated).
static void pcm_put32 (u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static enumError pcm_write_wav (ccp arg, ccp basedir, uint ch, uint rate, uint bits, const u8 *pcm, size_t n)
{
	u8 *wav = MALLOC (44 + n);
	if (!wav)
		return ERR_OUT_OF_MEMORY;
	memcpy (wav, "RIFF", 4); pcm_put32 (wav + 4, 36 + n);
	memcpy (wav + 8, "WAVEfmt ", 8); pcm_put32 (wav + 16, 16);
	wav[20] = 1; wav[21] = 0; wav[22] = ch; wav[23] = 0;
	pcm_put32 (wav + 24, rate); pcm_put32 (wav + 28, rate * ch * bits / 8);
	wav[32] = ch * bits / 8; wav[33] = 0; wav[34] = bits; wav[35] = 0;
	memcpy (wav + 36, "data", 4); pcm_put32 (wav + 40, n);
	memcpy (wav + 44, pcm, n);
	char stem[PATH_MAX], fn[PATH_MAX], dest[PATH_MAX];
	const char *sl = strrchr (arg, '/');
	snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
	char *dot = strrchr (stem, '.');
	if (dot)
		*dot = 0;
	snprintf (fn, sizeof (fn), "%s.wav", stem);
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PCM-AUDIO:%s -> %s/%s%s (%u ch, %u Hz, %u bit, %zu bytes)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, basedir ? basedir : "", fn, ch, rate, bits, n);
	const enumError err = testmode ? ERR_OK : shade_write (arg, dest, basedir, fn, wav, (uint)(44 + n));
	FREE (wav);
	return err;
}

static enumError extract_pcmaudio_file (ccp arg, ccp basedir, uint depth)
{
	const bool aif = is_ext (arg, ".aif") || is_ext (arg, ".aiff") || is_ext (arg, ".aifc");
	const bool au = is_ext (arg, ".au") || is_ext (arg, ".snd");
	const bool voc = is_ext (arg, ".voc");
	if ((!aif && !au && !voc) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (aif && sz > 12 && !memcmp (raw, "FORM", 4) && (!memcmp (raw + 8, "AIFF", 4) || !memcmp (raw + 8, "AIFC", 4)))
	{
		uint ch = 0, bits = 0, rate = 0;
		bool le = false;
		const u8 *ssnd = 0;
		size_t ssnd_n = 0;
		for (size_t p = 12; p + 8 <= sz;)
		{
			const size_t cs = rd_be32 (raw + p + 4);
			const u8 *c = raw + p + 8;
			if (p + 8 + cs > sz)
				break;
			if (!memcmp (raw + p, "COMM", 4) && cs >= 18)
			{
				ch = c[0] << 8 | c[1];
				bits = c[6] << 8 | c[7];
				const int ex = (c[8] & 0x7f) << 8 | c[9];
				const uint hi = rd_be32 (c + 10);
				rate = ex ? (uint)((double)hi * pow (2.0, ex - 16383 - 31) + 0.5) : 0;
				if (cs >= 22 && !memcmp (raw + 8, "AIFC", 4))
				{
					if (!memcmp (c + 18, "sowt", 4))
						le = true;
					else if (memcmp (c + 18, "NONE", 4) && memcmp (c + 18, "twos", 4))
						ch = 0; // compressed AIFF-C
				}
			}
			else if (!memcmp (raw + p, "SSND", 4) && cs >= 8)
			{
				const size_t off = rd_be32 (c);
				if (8 + off <= cs)
				{
					ssnd = c + 8 + off;
					ssnd_n = cs - 8 - off;
				}
			}
			p += 8 + cs + (cs & 1);
		}
		if (ch && ch < 9 && rate && bits >= 8 && bits <= 32 && ssnd)
		{
			const uint bytes = (bits + 7) / 8;
			const size_t n = ssnd_n / bytes * bytes;
			u8 *out = MALLOC (n ? n : 1);
			if (out)
			{
				for (size_t i = 0; i < n; i += bytes)
					for (uint b = 0; b < bytes; b++)
					{
						u8 v = ssnd[i + (le ? b : bytes - 1 - b)];
						if (bytes == 1)
							v ^= 0x80;
						out[i + b] = v;
					}
				err = pcm_write_wav (arg, basedir, ch, rate, bytes * 8, out, n);
				FREE (out);
			}
		}
	}
	else if (au && sz > 24 && !memcmp (raw, ".snd", 4))
	{
		const size_t off = rd_be32 (raw + 4), len = rd_be32 (raw + 8);
		const uint enc = rd_be32 (raw + 12), rate = rd_be32 (raw + 16), ch = rd_be32 (raw + 20);
		size_t n = len == 0xffffffffu || off + len > sz ? sz - off : len;
		if (off >= 24 && off < sz && ch && ch < 9 && rate)
		{
			const u8 *d = raw + off;
			if (enc == 1)
			{
				u8 *out = MALLOC (n * 2 + 2);
				if (out)
				{
					for (size_t i = 0; i < n; i++)
					{
						const int u = ~d[i] & 255, t = ((u & 15) << 3) + 132;
						int v = (t << ((u & 0x70) >> 4)) - 132;
						if (u & 0x80)
							v = -v;
						out[i * 2] = v & 255;
						out[i * 2 + 1] = (v >> 8) & 255;
					}
					err = pcm_write_wav (arg, basedir, ch, rate, 16, out, n * 2);
					FREE (out);
				}
			}
			else if (enc >= 2 && enc <= 5)
			{
				const uint bytes = enc - 1;
				n = n / bytes * bytes;
				u8 *out = MALLOC (n ? n : 1);
				if (out)
				{
					for (size_t i = 0; i < n; i += bytes)
						for (uint b = 0; b < bytes; b++)
							out[i + b] = d[i + bytes - 1 - b] ^ (bytes == 1 ? 0 : 0);
					if (bytes == 1)
						for (size_t i = 0; i < n; i++)
							out[i] ^= 0x80;
					err = pcm_write_wav (arg, basedir, ch, rate, bytes * 8, out, n);
					FREE (out);
				}
			}
		}
	}
	else if (voc && sz > 26 && !memcmp (raw, "Creative Voice File\x1a", 20))
	{
		u8 *out = MALLOC (sz);
		size_t on = 0;
		uint rate = 0;
		bool ok = out != 0;
		for (size_t p = raw[20] | raw[21] << 8; ok && p < sz && raw[p];)
		{
			const uint type = raw[p];
			const size_t bl = p + 4 <= sz ? raw[p + 1] | raw[p + 2] << 8 | raw[p + 3] << 16 : sz;
			if (p + 4 + bl > sz)
				break;
			const u8 *b = raw + p + 4;
			if (type == 1 && bl >= 2)
			{
				if (b[1] != 0) { ok = false; break; }
				if (!rate)
					rate = 1000000 / (256 - b[0]);
				memcpy (out + on, b + 2, bl - 2);
				on += bl - 2;
			}
			else if (type == 2)
			{
				memcpy (out + on, b, bl);
				on += bl;
			}
			p += 4 + bl;
		}
		if (ok && on && rate)
			err = pcm_write_wav (arg, basedir, 1, rate, 8, out, on);
		FREE (out);
	}
	FREE (raw);
	return err;
}
