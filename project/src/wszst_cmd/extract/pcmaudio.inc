// Plain PCM audio containers converted to RIFF WAV ("<file>.d/<name>.wav"): AIFF / AIFF-C (".aif", ".aiff", ".aifc": big endian 8..32 bit PCM, "sowt" = little endian
// AIFF-C), Sun / NeXT ".au" (".snd" magic: 8..32 bit PCM and mu-law) and Creative Voice ".voc" (8 bit PCM data blocks, concatenated), Apple CAF ".caf" (linear PCM) and Sony Wave64 ".w64" (PCM).
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
	const bool voc = is_ext (arg, ".voc"), caf = is_ext (arg, ".caf"), w64 = is_ext (arg, ".w64");
	if ((!aif && !au && !voc && !caf && !w64) || (export_count == 0 && !opt_decode))
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
	else if (caf && sz > 20 && !memcmp (raw, "caff", 4))
	{
		uint ch = 0, bits = 0, rate = 0, flags = 0;
		const u8 *data = 0;
		size_t dn = 0;
		bool lpcm = false;
		for (size_t p = 8; p + 12 <= sz;)
		{
			const uint64_t cs = (uint64_t)rd_be32 (raw + p + 4) << 32 | rd_be32 (raw + p + 8);
			const u8 *c = raw + p + 12;
			if (!memcmp (raw + p, "desc", 4) && cs >= 32 && p + 12 + 32 <= sz)
			{
				uint64_t rb = (uint64_t)rd_be32 (c) << 32 | rd_be32 (c + 4);
				double r;
				memcpy (&r, &rb, 8);
				rate = (uint)(r + 0.5);
				lpcm = !memcmp (c + 8, "lpcm", 4);
				flags = rd_be32 (c + 12);
				ch = rd_be32 (c + 24);
				bits = rd_be32 (c + 28);
			}
			else if (!memcmp (raw + p, "data", 4) && cs >= 4)
			{
				data = c + 4;
				dn = cs == 0xffffffffffffffffull || p + 12 + cs > sz ? sz - p - 16 : cs - 4;
			}
			if (cs == 0xffffffffffffffffull || p + 12 + cs > sz)
				break;
			p += 12 + cs;
		}
		if (lpcm && data && ch && ch < 9 && rate && !(flags & 1) && (bits == 8 || bits == 16 || bits == 24 || bits == 32))
		{
			const uint bytes = bits / 8;
			const size_t n = dn / bytes * bytes;
			u8 *out = MALLOC (n ? n : 1);
			if (out)
			{
				const bool le = (flags & 2) != 0;
				for (size_t i = 0; i < n; i += bytes)
					for (uint b = 0; b < bytes; b++)
					{
						u8 v = data[i + (le ? b : bytes - 1 - b)];
						if (bytes == 1)
							v ^= 0x80;
						out[i + b] = v;
					}
				err = pcm_write_wav (arg, basedir, ch, rate, bits, out, n);
				FREE (out);
			}
		}
	}
	else if (w64 && sz > 40 && !memcmp (raw, "riff\x2e\x91\xcf\x11\xa5\xd6\x28\xdb\x04\xc1\x00\x00", 16))
	{
		static const u8 fmt_g[16] = {0x66, 0x6d, 0x74, 0x20, 0xf3, 0xac, 0xd3, 0x11, 0x8c, 0xd1, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a};
		static const u8 data_g[16] = {0x64, 0x61, 0x74, 0x61, 0xf3, 0xac, 0xd3, 0x11, 0x8c, 0xd1, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a};
		uint tag = 0, ch = 0, rate = 0, bits = 0;
		const u8 *data = 0;
		size_t dn = 0;
		for (size_t p = 40; p + 24 <= sz;)
		{
			const size_t cs = raw[p + 16] | raw[p + 17] << 8 | raw[p + 18] << 16 | (size_t)raw[p + 19] << 24;
			if (cs < 24 || (cs > 0x7fffffff))
				break;
			const u8 *c = raw + p + 24;
			const size_t body = cs - 24;
			if (!memcmp (raw + p, fmt_g, 16) && body >= 16 && p + cs <= sz)
			{
				tag = c[0] | c[1] << 8; ch = c[2] | c[3] << 8; rate = c[4] | c[5] << 8 | c[6] << 16 | (uint)c[7] << 24; bits = c[14] | c[15] << 8;
				if (tag == 0xfffe && body >= 40)
					tag = c[24] | c[25] << 8; // WAVE_FORMAT_EXTENSIBLE: the sub format GUID starts with the real tag
			}
			else if (!memcmp (raw + p, data_g, 16))
			{
				data = c;
				dn = p + cs <= sz ? body : sz - p - 24;
			}
			p += (cs + 7) & ~(size_t)7;
		}
		if (tag == 1 && data && ch && ch < 9 && rate && (bits == 8 || bits == 16 || bits == 24 || bits == 32))
			err = pcm_write_wav (arg, basedir, ch, rate, bits, data, dn / (bits / 8) * (bits / 8));
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
