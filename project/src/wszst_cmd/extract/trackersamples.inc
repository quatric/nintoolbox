// Scream Tracker 3 ".s3m" ("SCRM": instrument parapointers after the order list, PCM sample at a 16 byte aligned segment, 8 / 16 bit, signed or unsigned, c2spd rate)
// , Impulse Tracker ".it" (IMPS sample headers, raw or IT214 / IT215 compressed) and FastTracker 2 ".xm" ("Extended Module: ": patterns skipped, per instrument 40 byte sample headers, delta coded 8 / 16 bit data) modules: every sample is written to
// "<file>.d/sample_NN.wav" (mono) and "info.txt" lists the title, channel count and samples.
static void trk_write_wav (ccp arg, ccp basedir, ccp dest, uint index, uint rate, uint bits, const u8 *pcm, size_t n, enumError *err)
{
	u8 *wav = MALLOC (44 + n);
	if (!wav)
	{
		*err = ERR_OUT_OF_MEMORY;
		return;
	}
	memcpy (wav, "RIFF", 4); pcm_put32 (wav + 4, 36 + n);
	memcpy (wav + 8, "WAVEfmt ", 8); pcm_put32 (wav + 16, 16);
	wav[20] = 1; wav[21] = 0; wav[22] = 1; wav[23] = 0;
	pcm_put32 (wav + 24, rate); pcm_put32 (wav + 28, rate * bits / 8);
	wav[32] = bits / 8; wav[33] = 0; wav[34] = bits; wav[35] = 0;
	memcpy (wav + 36, "data", 4); pcm_put32 (wav + 40, n);
	memcpy (wav + 44, pcm, n);
	char fn[64];
	snprintf (fn, sizeof (fn), "sample_%02u.wav", index);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT TRACKER:%s -> %s/%s%s (%zu bytes)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, basedir ? basedir : "", fn, n);
	*err = shade_write (arg, dest, basedir, fn, wav, (uint)(44 + n));
	FREE (wav);
}


typedef struct it_bits_t { const u8 *p; size_t n, pos; uint bit; } it_bits_t;
static uint it_get (it_bits_t *b, uint nb)
{
	uint v = 0;
	for (uint i = 0; i < nb; i++)
	{
		const uint x = b->pos < b->n ? b->p[b->pos] >> b->bit & 1 : 0;
		v |= x << i;
		if (++b->bit == 8) { b->bit = 0; b->pos++; }
	}
	return v;
}

// IT214 / IT215 sample decompression; out gets "frames" samples of 8 or 16 bit (little endian)
static bool it_unpack (const u8 *src, size_t n, u8 *out, size_t frames, bool b16, bool it215)
{
	size_t p = 0, o = 0;
	while (o < frames)
	{
		if (p + 2 > n)
			return false;
		const size_t clen = src[p] | src[p + 1] << 8;
		p += 2;
		if (p + clen > n)
			return false;
		it_bits_t b = {src + p, clen, 0, 0};
		const size_t blk = frames - o < (b16 ? 0x4000u : 0x8000u) ? frames - o : (b16 ? 0x4000u : 0x8000u);
		uint width = b16 ? 17 : 9;
		int d1 = 0, d2 = 0;
		for (size_t k = 0; k < blk;)
		{
			if (b.pos >= clen + 1)
				return false;
			uint v = it_get (&b, width);
			const uint hi = b16 ? 16 : 8;
			if (width < 7)
			{
				if (v == 1u << (width - 1))
				{
					const uint nw = it_get (&b, b16 ? 4 : 3) + 1;
					width = nw < width ? nw : nw + 1;
					continue;
				}
			}
			else if (width < hi + 1)
			{
				const uint border = ((b16 ? 0xffffu : 0xffu) >> (hi + 1 - width)) - (b16 ? 8 : 4);
				if (v > border && v <= border + (b16 ? 16u : 8u))
				{
					v -= border;
					width = v < width ? v : v + 1;
					continue;
				}
			}
			else if (v & (1u << hi))
			{
				width = (v + 1) & 0xff;
				continue;
			}
			int val;
			if (b16)
			{
				if (width < 16) { const uint sh = 16 - width; val = (int16_t)(v << sh) >> sh; } else val = (int16_t)v;
				d1 = (int16_t)(d1 + val);
				d2 = (int16_t)(d2 + d1);
				const int r = it215 ? d2 : d1;
				out[(o + k) * 2] = r & 255; out[(o + k) * 2 + 1] = (r >> 8) & 255;
			}
			else
			{
				if (width < 8) { const uint sh = 8 - width; val = (int8_t)(v << sh) >> sh; } else val = (int8_t)v;
				d1 = (int8_t)(d1 + val);
				d2 = (int8_t)(d2 + d1);
				out[o + k] = (u8)(it215 ? d2 : d1);
			}
			k++;
		}
		o += blk;
		p += clen;
	}
	return true;
}

static enumError extract_trackersamples_file (ccp arg, ccp basedir, uint depth)
{
	const bool s3m = is_ext (arg, ".s3m"), xm = is_ext (arg, ".xm"), it = is_ext (arg, ".it");
	if ((!s3m && !xm && !it) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	char dest[PATH_MAX], title[40];
	beside_source_dest (dest, sizeof (dest), arg);
	retro_info_t info = {{0}, 0};
	uint count = 0;
#define LE16(p) ((uint)(p)[0] | (uint)(p)[1] << 8)
#define LE32(p) ((u32)(p)[0] | (u32)(p)[1] << 8 | (u32)(p)[2] << 16 | (u32)(p)[3] << 24)
	if (s3m && sz > 0x60 && !memcmp (raw + 0x2c, "SCRM", 4))
	{
		retro_text (title, raw, 28);
		const uint ord = LE16 (raw + 0x20), ins = LE16 (raw + 0x22), ffi = LE16 (raw + 0x2a);
		retro_info (&info, "format: Scream Tracker 3 module\ntitle: %s\norders: %u\ninstruments: %u\npatterns: %u\n", title, ord, ins, LE16 (raw + 0x24));
		err = ERR_OK;
		for (uint i = 0; i < ins && err == ERR_OK; i++)
		{
			const size_t pp = 0x60 + ord + (size_t)i * 2;
			if (pp + 2 > sz)
				break;
			const size_t io = (size_t)LE16 (raw + pp) * 16;
			if (!io || io + 0x50 > sz || raw[io] != 1)
				continue;
			const size_t so = ((size_t)raw[io + 13] << 16 | LE16 (raw + io + 14)) * 16, len = LE32 (raw + io + 16);
			const uint flags = raw[io + 31], rate = LE32 (raw + io + 32), bits = flags & 4 ? 16 : 8, chans = flags & 2 ? 2 : 1;
			char nm[32];
			retro_text (nm, raw + io + 0x30, 28);
			const size_t bytes = len * (bits / 8) * chans;
			if (!len || !so || so + bytes > sz || len > 0x4000000)
				continue;
			retro_info (&info, "sample %02u: \"%s\" %zu frames, %u bit%s, %u Hz, volume %u\n", i + 1, nm, len, bits, chans == 2 ? " stereo (written as stored)" : "", rate, raw[io + 28]);
			u8 *pcm = MALLOC (bytes);
			if (!pcm)
				continue;
			for (size_t k = 0; k < bytes; k += bits / 8)
			{
				if (bits == 8)
					pcm[k] = ffi == 2 ? raw[so + k] : raw[so + k] ^ 0x80;
				else
				{
					uint v = LE16 (raw + so + k);
					if (ffi == 2)
						v = (v - 0x8000) & 0xffff;
					pcm[k] = v & 255; pcm[k + 1] = v >> 8;
				}
			}
			trk_write_wav (arg, basedir, dest, i + 1, rate ? rate : 8363, bits, pcm, bytes, &err);
			FREE (pcm);
			count++;
		}
	}
	else if (xm && sz > 80 && !memcmp (raw, "Extended Module: ", 17))
	{
		retro_text (title, raw + 17, 20);
		const size_t hs = LE32 (raw + 60);
		const uint nch = LE16 (raw + 68), npat = LE16 (raw + 70), nins = LE16 (raw + 72);
		retro_info (&info, "format: FastTracker 2 module\ntitle: %s\nchannels: %u\npatterns: %u\ninstruments: %u\n", title, nch, npat, nins);
		size_t p = 60 + hs;
		bool ok = p < sz;
		for (uint i = 0; ok && i < npat; i++)
		{
			if (p + 9 > sz) { ok = false; break; }
			p += LE32 (raw + p) + LE16 (raw + p + 7);
		}
		err = ok ? ERR_OK : ERR_NOTHING_TO_DO;
		uint sidx = 0;
		for (uint i = 0; ok && i < nins && err == ERR_OK; i++)
		{
			if (p + 29 > sz) break;
			const size_t isz = LE32 (raw + p);
			const uint ns = LE16 (raw + p + 27);
			size_t sh = p + isz;
			if (ns && p + 33 <= sz)
				sh = p + isz; // sample headers directly follow the instrument header
			size_t data = sh + (size_t)ns * 40;
			if (data > sz) break;
			for (uint s = 0; s < ns && err == ERR_OK; s++)
			{
				const u8 *h = raw + sh + (size_t)s * 40;
				const size_t len = LE32 (h);
				const bool b16 = (h[14] & 16) != 0;
				if (data + len > sz) { ok = false; break; }
				char nm[32];
				retro_text (nm, h + 18, 22);
				if (len)
				{
					const uint frames = len / (b16 ? 2 : 1);
					const int rel = (int8_t)h[16], fine = (int8_t)h[13];
					const uint rate = (uint)(8363.0 * pow (2.0, (rel * 128 + fine) / 1536.0) + 0.5);
					retro_info (&info, "sample %02u: \"%s\" %u frames, %u bit, volume %u, relative note %d, finetune %d\n", sidx, nm, frames, b16 ? 16 : 8, h[12], rel, fine);
					u8 *pcm = MALLOC (len);
					if (pcm)
					{
						int acc = 0;
						for (size_t k = 0; k < len; k += b16 ? 2 : 1)
						{
							if (b16)
							{
								acc = (int16_t)(acc + (int16_t)LE16 (raw + data + k));
								pcm[k] = acc & 255; pcm[k + 1] = (acc >> 8) & 255;
							}
							else
							{
								acc = (int8_t)(acc + (int8_t)raw[data + k]);
								pcm[k] = (u8)(acc ^ 0x80);
							}
						}
						trk_write_wav (arg, basedir, dest, sidx, rate, b16 ? 16 : 8, pcm, len, &err);
						FREE (pcm);
						count++;
					}
				}
				sidx++;
				data += len;
			}
			p = data;
		}
	}
	else if (it && sz > 0xc0 && !memcmp (raw, "IMPM", 4))
	{
		retro_text (title, raw + 4, 26);
		const uint ord = LE16 (raw + 0x20), nins = LE16 (raw + 0x22), nsmp = LE16 (raw + 0x24), cmwt = LE16 (raw + 0x2a);
		retro_info (&info, "format: Impulse Tracker module\ntitle: %s\norders: %u\ninstruments: %u\nsamples: %u\npatterns: %u\n", title, ord, nins, nsmp, LE16 (raw + 0x26));
		err = ERR_OK;
		for (uint i = 0; i < nsmp && err == ERR_OK; i++)
		{
			const size_t pp = 0xc0 + ord + (size_t)nins * 4 + (size_t)i * 4;
			if (pp + 4 > sz)
				break;
			const size_t io = LE32 (raw + pp);
			if (io + 0x50 > sz || memcmp (raw + io, "IMPS", 4))
				continue;
			const uint flags = raw[io + 0x12], cvt = raw[io + 0x2e];
			const size_t len = LE32 (raw + io + 0x30), so = LE32 (raw + io + 0x48);
			const uint rate = LE32 (raw + io + 0x3c);
			const bool b16 = (flags & 2) != 0, comp = (flags & 8) != 0;
			char nm[32];
			retro_text (nm, raw + io + 0x14, 26);
			if (!(flags & 1) || !len || (flags & 4) || len > 0x4000000 || so >= sz)
				continue;
			const size_t bytes = len * (b16 ? 2 : 1);
			u8 *pcm = MALLOC (bytes);
			if (!pcm)
				continue;
			bool ok;
			if (comp)
				ok = it_unpack (raw + so, sz - so, pcm, len, b16, cmwt >= 0x215);
			else
			{
				ok = so + bytes <= sz;
				if (ok)
					memcpy (pcm, raw + so, bytes);
			}
			if (ok)
			{
				// IT stores signed samples when cvt bit 0 is set; WAV wants unsigned 8 bit and signed 16 bit
				const bool is_signed = comp || (cvt & 1);
				for (size_t k = 0; k < bytes; k += b16 ? 2 : 1)
				{
					if (!b16 && is_signed)
						pcm[k] ^= 0x80;
					else if (b16 && !is_signed)
					{
						const uint v = (LE16 (pcm + k) - 0x8000) & 0xffff;
						pcm[k] = v & 255; pcm[k + 1] = v >> 8;
					}
				}
				retro_info (&info, "sample %02u: \"%s\" %zu frames, %u bit%s, %u Hz, volume %u\n", i + 1, nm, len, b16 ? 16 : 8, comp ? " (compressed)" : "", rate, raw[io + 0x13]);
				trk_write_wav (arg, basedir, dest, i + 1, rate ? rate : 8363, b16 ? 16 : 8, pcm, bytes, &err);
				count++;
			}
			FREE (pcm);
		}
	}
#undef LE16
#undef LE32
	if (count && err == ERR_OK)
		err = shade_write (arg, dest, basedir, "info.txt", (u8 *)info.buf, (uint)info.n);
	else if (!count && err == ERR_OK)
		err = ERR_NOTHING_TO_DO;
	FREE (raw);
	return err;
}
