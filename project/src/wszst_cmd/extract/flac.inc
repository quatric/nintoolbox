// FLAC ".flac": STREAMINFO + frames decoded to PCM "<file>.d/<name>.wav" (8 / 16 / 24 bit, 1-8 channels): constant, verbatim, fixed and LPC subframes, Rice residuals,
// wasted bits and the three stereo decorrelation modes. CRCs are not checked.
typedef struct flac_br_t { const u8 *p; size_t n, pos; uint bit; bool err; } flac_br_t;

static uint64_t flac_bits (flac_br_t *b, uint nb)
{
	uint64_t v = 0;
	while (nb)
	{
		if (b->pos >= b->n) { b->err = true; return 0; }
		const uint avail = 8 - b->bit, take = nb < avail ? nb : avail;
		v = v << take | (uint)((b->p[b->pos] >> (avail - take)) & ((1u << take) - 1));
		b->bit += take;
		if (b->bit == 8) { b->bit = 0; b->pos++; }
		nb -= take;
	}
	return v;
}
static int64_t flac_sbits (flac_br_t *b, uint nb)
{
	const uint64_t v = flac_bits (b, nb);
	return nb ? (int64_t)(v << (64 - nb)) >> (64 - nb) : 0;
}

static bool flac_residual (flac_br_t *b, int32_t *out, uint bs, uint order)
{
	const uint method = flac_bits (b, 2);
	if (method > 1)
		return false;
	const uint po = flac_bits (b, 4), parts = 1u << po;
	if ((bs >> po) < order)
		return false;
	uint k = order;
	for (uint pi = 0; pi < parts; pi++)
	{
		const uint n = (bs >> po) - (pi == 0 ? order : 0);
		const uint par = flac_bits (b, method ? 5 : 4);
		if (par == (method ? 31u : 15u))
		{
			const uint nb = flac_bits (b, 5);
			for (uint i = 0; i < n; i++)
				out[k++] = (int32_t)flac_sbits (b, nb);
		}
		else
			for (uint i = 0; i < n; i++)
			{
				uint q = 0;
				while (!flac_bits (b, 1))
				{
					if (b->err || ++q > 1u << 20) return false;
				}
				const uint64_t u = (uint64_t)q << par | (par ? flac_bits (b, par) : 0);
				out[k++] = (int32_t)(u >> 1) ^ -(int32_t)(u & 1);
			}
		if (b->err)
			return false;
	}
	return true;
}

static bool flac_subframe (flac_br_t *b, int32_t *out, uint bs, uint bps)
{
	if (flac_bits (b, 1))
		return false;
	const uint type = flac_bits (b, 6);
	uint wasted = 0;
	if (flac_bits (b, 1))
	{
		wasted = 1;
		while (!flac_bits (b, 1))
		{
			wasted++;
			if (b->err) return false;
		}
	}
	bps -= wasted;
	if (type == 0)
	{
		const int32_t v = (int32_t)flac_sbits (b, bps);
		for (uint i = 0; i < bs; i++) out[i] = v;
	}
	else if (type == 1)
		for (uint i = 0; i < bs; i++) out[i] = (int32_t)flac_sbits (b, bps);
	else if (type >= 8 && type <= 12)
	{
		const uint order = type - 8;
		for (uint i = 0; i < order && i < bs; i++) out[i] = (int32_t)flac_sbits (b, bps);
		if (!flac_residual (b, out, bs, order))
			return false;
		for (uint i = order; i < bs; i++)
		{
			int64_t p = 0;
			switch (order)
			{
				case 1: p = out[i - 1]; break;
				case 2: p = 2 * (int64_t)out[i - 1] - out[i - 2]; break;
				case 3: p = 3 * (int64_t)out[i - 1] - 3 * (int64_t)out[i - 2] + out[i - 3]; break;
				case 4: p = 4 * (int64_t)out[i - 1] - 6 * (int64_t)out[i - 2] + 4 * (int64_t)out[i - 3] - out[i - 4]; break;
			}
			out[i] = (int32_t)(p + out[i]);
		}
	}
	else if (type >= 32)
	{
		const uint order = type - 31;
		if (order >= bs) return false;
		int32_t warm[32];
		for (uint i = 0; i < order; i++) out[i] = warm[i] = (int32_t)flac_sbits (b, bps);
		const uint prec = flac_bits (b, 4) + 1;
		const int shift = (int)flac_sbits (b, 5);
		int32_t coef[32];
		for (uint i = 0; i < order; i++) coef[i] = (int32_t)flac_sbits (b, prec);
		if (prec == 16 && false) return false;
		if (!flac_residual (b, out, bs, order))
			return false;
		for (uint i = order; i < bs; i++)
		{
			int64_t sum = 0;
			for (uint j = 0; j < order; j++)
				sum += (int64_t)coef[j] * out[i - 1 - j];
			out[i] = (int32_t)((shift >= 0 ? sum >> shift : sum << -shift) + out[i]);
		}
	}
	else
		return false;
	if (wasted)
		for (uint i = 0; i < bs; i++) out[i] = (int32_t)((uint32_t)out[i] << wasted);
	return !b->err;
}

static enumError extract_flac_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".flac") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 42 && !memcmp (raw, "fLaC", 4))
	{
		size_t p = 4;
		uint rate = 0, ch = 0, bps = 0;
		uint64_t total = 0;
		bool last = false;
		while (!last && p + 4 <= sz)
		{
			const uint type = raw[p] & 127, len = raw[p + 1] << 16 | raw[p + 2] << 8 | raw[p + 3];
			last = raw[p] >> 7;
			if (type == 0 && len >= 34 && p + 4 + len <= sz)
			{
				const u8 *si = raw + p + 4;
				rate = si[10] << 12 | si[11] << 4 | si[12] >> 4;
				ch = (si[12] >> 1 & 7) + 1;
				bps = ((si[12] & 1) << 4 | si[13] >> 4) + 1;
				total = (uint64_t)(si[13] & 15) << 32 | (uint64_t)si[14] << 24 | si[15] << 16 | si[16] << 8 | si[17];
			}
			p += 4 + len;
		}
		if (rate && ch && ch <= 8 && (bps == 8 || bps == 16 || bps == 24))
		{
			const uint bytes = bps / 8;
			size_t cap = total ? (size_t)total * ch * bytes : (sz - p) * 8;
			if (cap > 0x60000000u) cap = 0x60000000u;
			u8 *wav = MALLOC (44 + cap + 65536);
			int32_t *buf[8] = {0};
			bool ok = wav != 0;
			for (uint c = 0; c < ch && ok; c++)
			{
				buf[c] = MALLOC (65536 * sizeof (int32_t));
				ok = buf[c] != 0;
			}
			size_t o = 44;
			flac_br_t b = {raw + p, sz - p, 0, 0, false};
			while (ok && b.pos + 6 < b.n)
			{
				if (!(raw[p + b.pos] == 0xff && (raw[p + b.pos + 1] & 0xfe) == 0xf8))
					break;
				flac_bits (&b, 14);
				flac_bits (&b, 2);
				const uint bsc = flac_bits (&b, 4), src = flac_bits (&b, 4), chan = flac_bits (&b, 4), ssc = flac_bits (&b, 3);
				flac_bits (&b, 1);
				// utf8 coded frame / sample number
				const uint lead = flac_bits (&b, 8);
				uint extra = lead >= 0xfc ? 5 : lead >= 0xf8 ? 4 : lead >= 0xf0 ? 3 : lead >= 0xe0 ? 2 : lead >= 0xc0 ? 1 : 0;
				if (lead == 0xff) extra = 6;
				while (extra--) flac_bits (&b, 8);
				uint bs = bsc == 1 ? 192 : bsc >= 2 && bsc <= 5 ? 576u << (bsc - 2) : bsc >= 8 ? 256u << (bsc - 8) : 0;
				if (bsc == 6) bs = flac_bits (&b, 8) + 1;
				else if (bsc == 7) bs = flac_bits (&b, 16) + 1;
				if (src == 12) flac_bits (&b, 8);
				else if (src == 13 || src == 14) flac_bits (&b, 16);
				flac_bits (&b, 8); // crc-8
				const uint fbps = ssc == 0 ? bps : ssc == 1 ? 8 : ssc == 2 ? 12 : ssc == 4 ? 16 : ssc == 5 ? 20 : ssc == 6 ? 24 : 0;
				uint fch = chan < 8 ? chan + 1 : 2;
				if (b.err || !bs || bs > 65535 || !fbps || fch != ch || chan > 10)
					{ ok = o > 44; break; }
				for (uint c = 0; c < fch && ok; c++)
					ok = flac_subframe (&b, buf[c], bs, fbps + (chan == 8 && c == 1) + (chan == 9 && c == 0) + (chan == 10 && c == 1));
				if (!ok)
					break;
				if (b.bit) { b.bit = 0; b.pos++; }
				flac_bits (&b, 16); // crc-16
				if (chan == 8) for (uint i = 0; i < bs; i++) buf[1][i] = buf[0][i] - buf[1][i];
				else if (chan == 9) for (uint i = 0; i < bs; i++) buf[0][i] += buf[1][i];
				else if (chan == 10)
					for (uint i = 0; i < bs; i++)
					{
						const int32_t mid = (int32_t)((uint32_t)buf[0][i] << 1 | ((uint32_t)buf[1][i] & 1)), side = buf[1][i];
						buf[0][i] = (mid + side) >> 1;
						buf[1][i] = (mid - side) >> 1;
					}
				if (o + (size_t)bs * ch * bytes > 44 + cap + 65536) { ok = false; break; }
				for (uint i = 0; i < bs; i++)
					for (uint c = 0; c < ch; c++)
					{
						const int32_t v = buf[c][i];
						if (bytes == 1) wav[o++] = (u8)(v + 128);
						else { wav[o++] = v & 255; wav[o++] = (v >> 8) & 255; if (bytes == 3) wav[o++] = (v >> 16) & 255; }
					}
			}
			if (wav && o > 44)
			{
				memcpy (wav, "RIFF", 4); pcm_put32 (wav + 4, o - 8);
				memcpy (wav + 8, "WAVEfmt ", 8); pcm_put32 (wav + 16, 16);
				wav[20] = 1; wav[21] = 0; wav[22] = ch; wav[23] = 0;
				pcm_put32 (wav + 24, rate); pcm_put32 (wav + 28, rate * ch * bytes);
				wav[32] = ch * bytes; wav[33] = 0; wav[34] = bps; wav[35] = 0;
				memcpy (wav + 36, "data", 4); pcm_put32 (wav + 40, o - 44);
				char stem[PATH_MAX], fn[PATH_MAX], dest[PATH_MAX];
				const char *sl = strrchr (arg, '/');
				snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
				char *dot = strrchr (stem, '.');
				if (dot) *dot = 0;
				snprintf (fn, sizeof (fn), "%s.wav", stem);
				beside_source_dest (dest, sizeof (dest), arg);
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sEXTRACT FLAC:%s -> %s/%s%s (%u ch, %u Hz, %u bit, %zu bytes)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, basedir ? basedir : "", fn, ch, rate, bps, o - 44);
				err = testmode ? ERR_OK : shade_write (arg, dest, basedir, fn, wav, (uint)o);
			}
			for (uint c = 0; c < 8; c++)
				FREE (buf[c]);
			FREE (wav);
		}
	}
	FREE (raw);
	return err;
}
