// EA "BNKb" sound banks (big-endian BNK, GameCube/Wii; Tiger Woods PGA Tour
// "abnk_*.bin" hole-archive members): +0x00 "BNKb", +0x04 u8 version (4/5),
// +0x06 u16 count, +0x14 count * u32 offsets relative to their own slot (0 = unused).
// Each entry is an EA "PT" tag header (platform u16 LE at +2, then {tag, len, value}
// patches: 0xa0 codec 0x12 = GC DSP, 0x84 rate, 0x85 samples, 0x88 absolute data
// offset, 0x8f 33 bytes with the 16 DSP coefficients). Each sound is written as
// "NNNN.dsp". "ABKC" soundbanks (FIFA, `.abk`) embed the same bank at +0x20.
typedef struct eabnk_snd_t
{
	uint codec, rate, samples, ch;
	size_t data, coefs;
} eabnk_snd_t;

// parse the PT tag header of slot i; false if the slot is empty or malformed
static bool eabnk_parse (const u8 *raw, size_t sz, uint i, eabnk_snd_t *s)
{
	memset (s, 0, sizeof (*s));
	s->ch = 1;
	const size_t slot = 0x14 + 4 * (size_t)i;
	const u32 rel = rd_be32 (raw + slot);
	if (!rel || rel >= sz - slot || sz - slot - rel < 8)
		return false;
	size_t p = slot + rel;
	if (raw[p] != 'P' || raw[p + 1] != 'T')
		return false;
	p += 4;
	while (p < sz)
	{
		const uint tag = raw[p++];
		if (tag == 0xff)
			break;
		if (tag == 0xfc || tag == 0xfd)
			continue;
		if (p >= sz)
			break;
		const uint len = raw[p++];
		if (len == 0xff || p + len > sz)
			break;
		uint v = 0;
		if (len <= 4)
			for (uint k = 0; k < len; k++)
				v = (v << 8) | raw[p + k];
		switch (tag)
		{
		case 0xa0: s->codec = v; break;
		case 0x82: s->ch = v; break;
		case 0x84: s->rate = v; break;
		case 0x85: s->samples = v; break;
		case 0x88: s->data = v; break;
		case 0x8f: s->coefs = p; break;
		}
		p += len;
	}
	return true;
}

// MSB-first bit helpers for the EALayer3 -> MPEG rebuild
static uint eal3_get (const u8 *b, size_t *pos, uint n)
{
	uint v = 0;
	while (n--)
	{
		v = (v << 1) | ((b[*pos >> 3] >> (7 - (*pos & 7))) & 1);
		(*pos)++;
	}
	return v;
}

static void eal3_put (u8 *b, size_t *pos, uint n, uint v)
{
	while (n--)
	{
		if ((v >> n) & 1)
			b[*pos >> 3] |= 0x80 >> (*pos & 7);
		(*pos)++;
	}
}

typedef struct eal3_gr_t
{
	uint ver_idx, sr_idx, ch_mode, mode_ext, gi, ch, mpeg1, sr;
	uint scfsi[2], size[2], o1[2], o2[2];
	size_t data_bit, data_bits, frame_bytes;
} eal3_gr_t;

// parse one EALayer3 v1 frame at p; returns bytes consumed (0 = invalid). *granule is
// set when the frame carries a granule, otherwise it was a PCM-only block.
static size_t eal3_parse (const u8 *raw, size_t p, size_t end, uint pcm_ch, eal3_gr_t *g, bool *granule)
{
	static const int sr_tab[4][4] = {{11025, 12000, 8000, 0}, {0, 0, 0, 0}, {22050, 24000, 16000, 0}, {44100, 48000, 32000, 0}};
	*granule = false;
	if (p >= end)
		return 0;
	if (raw[p] == 0xee)
	{
		if (p + 5 > end)
			return 0;
		const size_t n = 5 + 2 * (size_t)((raw[p + 3] << 8) | raw[p + 4]) * pcm_ch;
		return p + n > end ? 0 : n;
	}
	if (raw[p])
		return 0;
	memset (g, 0, sizeof (*g));
	size_t pos = 8;
	const u8 *b = raw + p;
	if (p + 4 > end)
		return 0;
	g->ver_idx = eal3_get (b, &pos, 2);
	g->sr_idx = eal3_get (b, &pos, 2);
	g->ch_mode = eal3_get (b, &pos, 2);
	g->mode_ext = eal3_get (b, &pos, 2);
	if ((!g->ver_idx && !g->sr_idx && !g->ch_mode && !g->mode_ext) || g->ver_idx == 1 || !sr_tab[g->ver_idx][g->sr_idx])
		return 0;
	g->sr = sr_tab[g->ver_idx][g->sr_idx];
	g->mpeg1 = g->ver_idx == 3;
	g->ch = g->ch_mode == 3 ? 1 : 2;
	const uint o2bits = g->mpeg1 ? 15 : 19;
	const size_t side = 1 + (g->mpeg1 ? 0 : 0) + (size_t)g->ch * (12 + 32 + o2bits) + 4 * (size_t)g->ch;
	if (p + (pos + side + 7) / 8 > end)
		return 0;
	g->gi = eal3_get (b, &pos, 1);
	if (g->mpeg1 && g->gi == 1)
		for (uint c = 0; c < g->ch; c++)
			g->scfsi[c] = eal3_get (b, &pos, 4);
	for (uint c = 0; c < g->ch; c++)
	{
		g->size[c] = eal3_get (b, &pos, 12);
		g->o1[c] = eal3_get (b, &pos, 32);
		g->o2[c] = eal3_get (b, &pos, o2bits);
		g->data_bits += g->size[c];
	}
	g->data_bit = pos;
	const size_t bytes = (pos + g->data_bits + 7) / 8;
	if (p + bytes > end)
		return 0;
	g->frame_bytes = bytes;
	*granule = true;
	return bytes;
}

// write one MPEG-1 (two granules) or MPEG-2 (one) Layer III frame; returns bytes
static size_t eal3_write (u8 *out, const u8 *raw0, const eal3_gr_t *g0, const u8 *raw1, const eal3_gr_t *g1)
{
	static const uint br1[15] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
	static const uint br2[15] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
	u8 tmp[8192];
	memset (tmp, 0, sizeof (tmp));
	size_t pos = 0;
	eal3_put (tmp, &pos, 11, 0x7ff);
	eal3_put (tmp, &pos, 2, g0->ver_idx);
	eal3_put (tmp, &pos, 2, 1);
	eal3_put (tmp, &pos, 1, 1);
	eal3_put (tmp, &pos, 4, 0); // bitrate patched below
	eal3_put (tmp, &pos, 2, g0->sr_idx);
	eal3_put (tmp, &pos, 2, 0);
	eal3_put (tmp, &pos, 2, g0->ch_mode);
	eal3_put (tmp, &pos, 2, g0->mode_ext);
	eal3_put (tmp, &pos, 2, 3);
	eal3_put (tmp, &pos, 2, 0);
	if (g0->mpeg1)
	{
		eal3_put (tmp, &pos, 9, 0);
		eal3_put (tmp, &pos, g0->ch == 1 ? 5 : 3, 0);
		for (uint c = 0; c < g1->ch; c++)
			eal3_put (tmp, &pos, 4, g1->scfsi[c]);
		for (int k = 0; k < 2; k++)
		{
			const eal3_gr_t *g = k ? g1 : g0;
			for (uint c = 0; c < g->ch; c++)
			{
				eal3_put (tmp, &pos, 12, g->size[c]);
				eal3_put (tmp, &pos, 32, g->o1[c]);
				eal3_put (tmp, &pos, 15, g->o2[c]);
			}
		}
	}
	else
	{
		eal3_put (tmp, &pos, 8, 0);
		eal3_put (tmp, &pos, g0->ch == 1 ? 1 : 2, 0);
		for (uint c = 0; c < g0->ch; c++)
		{
			eal3_put (tmp, &pos, 12, g0->size[c]);
			eal3_put (tmp, &pos, 32, g0->o1[c]);
			eal3_put (tmp, &pos, 19, g0->o2[c]);
		}
	}
	for (int k = 0; k < (g0->mpeg1 ? 2 : 1); k++)
	{
		const eal3_gr_t *g = k ? g1 : g0;
		const u8 *src = k ? raw1 : raw0;
		size_t sp = g->data_bit;
		for (uint c = 0; c < g->ch; c++)
			for (uint j = 0; j < g->size[c]; j++)
				eal3_put (tmp, &pos, 1, eal3_get (src, &sp, 1));
	}
	const size_t need = (pos + 7) / 8;
	// smallest standard bitrate whose frame holds everything (no bit reservoir needed)
	uint bi = 0;
	size_t fsize = 0;
	for (uint i = 1; i < 15; i++)
	{
		fsize = (size_t)(g0->mpeg1 ? 144 : 72) * (g0->mpeg1 ? br1[i] : br2[i]) * 1000 / g0->sr;
		if (fsize >= need)
		{
			bi = i;
			break;
		}
	}
	if (!bi) // free-format fallback: twice the largest bitrate
		fsize = (size_t)(g0->mpeg1 ? 144 : 72) * (g0->mpeg1 ? 640 : 320) * 1000 / g0->sr;
	if (fsize < need || fsize > sizeof (tmp))
		return 0;
	tmp[2] = (tmp[2] & 0x0f) | (bi << 4);
	memcpy (out, tmp, fsize);
	return fsize;
}

// EALayer3 (codec 0x17) bank sound -> plain MP3; returns malloc'd file or NULL
static u8 *eal3_to_mp3 (const u8 *raw, size_t start, size_t end, uint samples, uint ch, size_t *out_size)
{
	const size_t max_granules = ((size_t)samples + 1105 + 575) / 576 + 4;
	u8 *out = MALLOC (max_granules * 2300 + 4096);
	if (!out)
		return 0;
	size_t o = 0, p = start, granules = 0;
	while (p < end && granules < max_granules)
	{
		eal3_gr_t g0, g1;
		bool has;
		size_t n = eal3_parse (raw, p, end, ch, &g0, &has);
		if (!n)
			break;
		p += n;
		if (!has)
			continue;
		granules++;
		const u8 *r0 = raw + p - n, *r1 = 0;
		if (g0.mpeg1)
		{
			for (;;)
			{
				n = eal3_parse (raw, p, end, ch, &g1, &has);
				if (!n)
					break;
				p += n;
				if (has)
					break;
			}
			if (!n || g1.gi == g0.gi || g1.ver_idx != g0.ver_idx)
				break;
			r1 = raw + p - n;
			granules++;
		}
		const size_t w = eal3_write (out + o, r0, &g0, r1, &g1);
		if (!w)
			break;
		o += w;
	}
	if (!o)
	{
		FREE (out);
		return 0;
	}
	*out_size = o;
	return out;
}

static enumError eabnk_split (ccp arg, ccp basedir, uint depth, u8 *raw, size_t sz)
{
	const uint count = sz > 0x20 ? (raw[6] << 8) | raw[7] : 0;
	if (sz < 0x20 || memcmp (raw, "BNKb", 4) || raw[4] < 4 || raw[4] > 5 || !count || 0x14 + 4 * (size_t)count > sz)
		return ERR_NOTHING_TO_DO;
	nintendo_sarc_entry_t *entries = CALLOC (count, sizeof (*entries));
	if (!entries)
		return ERR_OUT_OF_MEMORY;
	uint n = 0;
	for (uint i = 0; i < count; i++)
	{
		eabnk_snd_t s;
		if (!eabnk_parse (raw, sz, i, &s) || !s.samples || !s.rate || !s.data || s.data >= sz)
			continue;
		u8 *out = 0;
		size_t out_size = 0;
		char name[32];
		if (s.codec == 0x12 && s.ch == 1 && s.coefs && s.coefs + 32 <= sz)
		{
			const size_t frames = ((size_t)s.samples + 13) / 14;
			const size_t bytes = frames * 8;
			if (bytes > sz - s.data)
				continue;
			out = CALLOC (1, 0x60 + bytes);
			if (!out)
				continue;
			wr_be32 (out + 0x00, s.samples);
			wr_be32 (out + 0x04, frames * 16);
			wr_be32 (out + 0x08, s.rate);
			wr_be32 (out + 0x10, 2);
			wr_be32 (out + 0x14, frames * 16 - 1);
			wr_be32 (out + 0x18, 2);
			memcpy (out + 0x1c, raw + s.coefs, 32);
			out[0x3f] = raw[s.data];
			memcpy (out + 0x60, raw + s.data, bytes);
			out_size = 0x60 + bytes;
			snprintf (name, sizeof (name), "%04u.dsp", i);
		}
		else if (s.codec == 0x17 && s.ch >= 1 && s.ch <= 2)
		{
			// the sound ends where the next one in the bank starts
			size_t limit = sz;
			for (uint k = 0; k < count; k++)
			{
				eabnk_snd_t o;
				if (k != i && eabnk_parse (raw, sz, k, &o) && o.data > s.data && o.data < limit)
					limit = o.data;
			}
			out = eal3_to_mp3 (raw, s.data, limit, s.samples, s.ch, &out_size);
			if (!out)
				continue;
			snprintf (name, sizeof (name), "%04u.mp3", i);
		}
		else
			continue;
		const bool ok = OwnedEntryAdd (entries, n, name, out, out_size);
		FREE (out);
		if (!ok)
			break;
		n++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (n)
		err = write_owned_entries (arg, basedir, depth, "EABNK", entries, n);
	else
		FREE (entries);
	return err;
}

static enumError extract_eabnk_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz >= 0x40 && !memcmp (raw, "BNKb", 4))
		err = eabnk_split (arg, basedir, depth, raw, sz);
	else if (sz >= 0x40 && !memcmp (raw, "ABKC", 4) && (is_ext (arg, ".abk") || is_ext (arg, ".bin")))
	{
		// ABKC soundbank (EA Sports Wii): big-endian header, +0x20 = offset of the embedded BNK
		const u32 bnk = rd_be32 (raw + 0x20);
		if (bnk >= 0x28 && bnk <= sz - 0x20 && !memcmp (raw + bnk, "BNKb", 4))
			err = eabnk_split (arg, basedir, depth, raw + bnk, sz - bnk);
	}
	FREE (raw);
	return err;
}
