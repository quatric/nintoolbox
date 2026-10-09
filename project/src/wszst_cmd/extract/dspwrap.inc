// Interleaved GameCube/Wii DSP-ADPCM streams wrapped in a small game-specific
// header: one standard 0x60-byte DSP header per channel followed by the ADPCM
// data interleaved in fixed-size chunks. Every channel is written as "chN.dsp"
// (decoded to WAV by the DSP extractor). Layouts follow the publishers' loaders:
//   Traveller's Tales "IDSP" (.gcm/.idsp: LEGO games, Transformers): +0x04/+0x08
//     version pairs {1,0xc8}, {2,0xd2}, {3,0x12c}; +0x0c interleave
//   Next Level "IDSP" (.idsp: Mario Strikers Charged): +0x04 interleave, headers at 0x0c, stereo
//   Procyon "sadb" (.sad: Disaster: Day of Crisis): channels u8 at +0x32, headers at 0x80,
//     data start +0x48, interleave 0x10
//   Capcom "DSPW" (.dspw: Sengoku Basara 3): channels at +0x18, per-channel {header,data}
//     blocks of (size-0x20)/channels starting at 0x20
typedef struct dspwrap_t
{
	uint ch;
	size_t hdr, spacing, start, interleave;
	const char *name;
} dspwrap_t;

static bool dspwrap_probe (const u8 *raw, size_t sz, dspwrap_t *w)
{
	memset (w, 0, sizeof (*w));
	if (sz < 0x100)
		return false;
	if (!memcmp (raw, "IDSP", 4))
	{
		const u32 v = rd_be32 (raw + 4), s = rd_be32 (raw + 8);
		if ((v == 1 && s == 0xc8) || (v == 2 && s == 0xd2) || (v == 3 && s == 0x12c))
		{
			w->ch = v == 3 ? rd_be32 (raw + 0x10) : 2;
			w->hdr = v == 1 ? 0x10 : 0x20;
			w->spacing = 0x60;
			w->interleave = rd_be32 (raw + 0x0c);
			w->name = "IDSP-TT";
		}
		else
		{
			w->ch = 2;
			w->hdr = 0x0c;
			w->spacing = 0x60;
			w->interleave = rd_be32 (raw + 4);
			w->name = "IDSP-NL";
		}
		w->start = w->hdr + w->spacing * w->ch;
	}
	else if (!memcmp (raw, "sadb", 4))
	{
		w->ch = raw[0x32];
		w->hdr = 0x80;
		w->spacing = 0x60;
		w->start = rd_be32 (raw + 0x48);
		w->interleave = 0x10;
		w->name = "SADB";
	}
	else if (!memcmp (raw, "DSPW", 4))
	{
		w->ch = rd_be32 (raw + 0x18);
		size_t data = rd_be32 (raw + 8);
		if (data > sz || data < 0x20 + 0x60 || w->ch < 1 || w->ch > 6)
			return false;
		data -= 0x20;
		w->hdr = 0x20;
		w->spacing = data / w->ch;
		w->start = 0x20 + 0x60;
		w->interleave = data / w->ch;
		w->name = "DSPW";
		return w->spacing > 0x60;
	}
	else
		return false;
	return w->ch >= 1 && w->ch <= 8 && w->interleave && w->start < sz && w->hdr + w->spacing * (w->ch - 1) + 0x60 <= sz;
}

static enumError extract_dspwrap_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	dspwrap_t w;
	if (!dspwrap_probe (raw, sz, &w))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (w.ch, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	for (uint c = 0; c < w.ch; c++)
	{
		const u8 *h = raw + w.hdr + w.spacing * c;
		const u32 nibbles = rd_be32 (h + 4);
		const size_t per = (((size_t)nibbles + 15) / 16) * 8;
		if (!per || rd_be16 (h + 0x0e) != 0 /* format must be ADPCM */)
			break;
		const size_t full = per / w.interleave, rem = per % w.interleave;
		const size_t tail = w.start + full * w.ch * w.interleave > sz ? 0 : sz - (w.start + full * w.ch * w.interleave);
		size_t rem_stride = rem;
		if (full == 0 || tail >= (size_t)w.ch * w.interleave)
			rem_stride = w.interleave;
		u8 *out = CALLOC (1, 0x60 + per);
		if (!out)
			break;
		memcpy (out, h, 0x60);
		bool ok = true;
		for (size_t i = 0; i < full && ok; i++)
		{
			const size_t src = w.start + (i * w.ch + c) * w.interleave;
			if (src + w.interleave > sz)
				ok = false;
			else
				memcpy (out + 0x60 + i * w.interleave, raw + src, w.interleave);
		}
		if (ok && rem)
		{
			const size_t src = w.start + full * w.ch * w.interleave + c * rem_stride;
			if (src + rem > sz)
				ok = false;
			else
				memcpy (out + 0x60 + full * w.interleave, raw + src, rem);
		}
		char name[32];
		snprintf (name, sizeof (name), "ch%u.dsp", c);
		const bool added = ok && OwnedEntryAdd (entries, n, name, out, 0x60 + per);
		FREE (out);
		if (!added)
			break;
		n++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (n == w.ch)
		err = write_owned_entries (arg, basedir, depth, w.name, entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}

// Exient "WIIADPCM" (.adpcm; Need for Speed: Hot Pursuit, Angry Birds Star Wars): the first
// channel header sits at 0x10 (v1: one more channel offset @+8) or 0x20 (v2: offsets @+8..),
// channel blocks are file-aligned multiples of the interleave (first offset - header offset); the first of each starts with
// the header bytes then its 0x60 DSP header and ADPCM, the following blocks hold pure data.
static enumError extract_wiiadpcm_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".adpcm"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x100 || memcmp (raw, "WIIADPCM", 8))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 test = rd_be32 (raw + 0x2c);
	const bool v1 = !(test == 0x10000 || test == 0);
	const size_t hdr = v1 ? 0x10 : 0x20;
	const uint maxch = v1 ? 2 : 6;
	uint ch = 1;
	for (uint i = 0; i + 1 < maxch; i++)
		if (rd_be32 (raw + 8 + 4 * i))
			ch++;
		else
			break;
	const size_t il = ch > 1 ? rd_be32 (raw + 8) - hdr : 0;
	if (ch > 1 && (il <= 0x60 || il > sz))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (ch, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	for (uint c = 0; c < ch; c++)
	{
		const size_t hp = hdr + (ch > 1 ? c * il : 0);
		if (hp + 0x60 > sz)
			break;
		const size_t per = (((size_t)rd_be32 (raw + hp + 4) + 15) / 16) * 8;
		u8 *out = CALLOC (1, 0x60 + per);
		if (!per || !out)
		{
			FREE (out);
			break;
		}
		memcpy (out, raw + hp, 0x60);
		size_t done = 0;
		bool ok = true;
		if (ch == 1)
		{
			ok = hp + 0x60 + per <= sz;
			if (ok)
				memcpy (out + 0x60, raw + hp + 0x60, per);
		}
		else
			for (size_t k = 0; done < per && ok; k++)
			{
				const size_t skip = k ? 0 : hdr + 0x60;
				const size_t src = (k * ch + c) * il + skip;
				size_t take = il - skip;
				if (take > per - done)
					take = per - done;
				if (src + take > sz)
					ok = false;
				else
				{
					memcpy (out + 0x60 + done, raw + src, take);
					done += take;
				}
			}
		char name[16];
		snprintf (name, sizeof (name), "ch%u.dsp", c);
		const bool added = ok && OwnedEntryAdd (entries, n, name, out, 0x60 + per);
		FREE (out);
		if (!added)
			break;
		n++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (n == ch)
		err = write_owned_entries (arg, basedir, depth, "WIIADPCM", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
