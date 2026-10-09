// Interleaved GameCube/Wii DSP-ADPCM streams wrapped in a small game-specific
// header: one standard 0x60-byte DSP header per channel followed by the ADPCM
// data interleaved in fixed-size chunks. Every channel is written as "chN.dsp"
// (decoded to WAV by the DSP extractor). Layouts follow the publishers' loaders:
//   Traveller's Tales "IDSP" (.gcm/.idsp: LEGO games, Transformers): +0x04/+0x08
//     version pairs {1,0xc8}, {2,0xd2}, {3,0x12c}; +0x0c interleave
//   Next Level "IDSP" (.idsp: Mario Strikers Charged): +0x04 interleave, headers at 0x0c, stereo
//   Procyon "sadb" (.sad: Disaster: Day of Crisis): channels u8 at +0x32, headers at 0x80,
//     data start +0x48, interleave 0x10
//   Paon "KNON"/"KAST" (.asr: Donkey Kong Barrel Blast): stereo, headers at 0x70, data +0x800, interleave 0x10
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
	else if (!memcmp (raw, "KNON", 4) && !memcmp (raw + 8, "WII ", 4) && !memcmp (raw + 0x20, "KAST", 4))
	{
		w->ch = 2;
		w->hdr = 0x70;
		w->spacing = 0x60;
		w->start = 0x800;
		w->interleave = 0x10;
		w->name = "KNON";
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
		if (!rd_be32 (h + 0x18))
			wr_be32 (out + 0x18, 2); // loaders that ignore the current address leave it unset
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
		if (ok && !rd_be16 (h + 0x3e)) // initial predictor/scale = first byte of the data
			out[0x3f] = out[0x60];
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

// Paon "KNON"/"KPST" (.str: Donkey Kong Barrel Blast): stereo 16-bit big-endian PCM from +0x800,
// 0x10-byte interleave, byte count @+0x3c, rate @+0x40 -> stereo.wav
static enumError extract_knonpcm_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t bytes = sz > 0x800 ? rd_be32 (raw + 0x3c) : 0;
	if (sz < 0x800 || memcmp (raw, "KNON", 4) || memcmp (raw + 8, "WII ", 4) || memcmp (raw + 0x20, "KPST", 4)
		|| bytes < 4 || bytes > sz - 0x800)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 rate = rd_be32 (raw + 0x40);
	const size_t frames = bytes / 4, data = frames * 4;
	u8 *wav = rate ? MALLOC (44 + data) : 0;
	nintendo_sarc_entry_t *entries = wav ? CALLOC (1, sizeof (*entries)) : 0;
	if (!entries)
	{
		FREE (wav);
		FREE (raw);
		return rate ? ERR_OUT_OF_MEMORY : ERR_NOTHING_TO_DO;
	}
	memcpy (wav, "RIFF", 4);
	wr_le32 (wav + 4, (u32)(36 + data));
	memcpy (wav + 8, "WAVEfmt ", 8);
	wr_le32 (wav + 16, 16);
	wr_le16 (wav + 20, 1);
	wr_le16 (wav + 22, 2);
	wr_le32 (wav + 24, rate);
	wr_le32 (wav + 28, rate * 4);
	wr_le16 (wav + 32, 4);
	wr_le16 (wav + 34, 16);
	memcpy (wav + 36, "data", 4);
	wr_le32 (wav + 40, (u32)data);
	// blocks of 0x10 bytes: 8 left samples, then 8 right samples
	for (size_t i = 0; i < frames; i++)
	{
		const size_t blk = i / 8, k = i % 8;
		const size_t src = 0x800 + blk * 0x20 + k * 2;
		const u8 *l = raw + src, *r = raw + src + 0x10;
		if (src + 0x12 > sz)
			wr_le32 (wav + 44 + 4 * i, 0);
		else
		{
			wr_le16 (wav + 44 + 4 * i, (u16)((l[0] << 8) | l[1]));
			wr_le16 (wav + 46 + 4 * i, (u16)((r[0] << 8) | r[1]));
		}
	}
	enumError err = ERR_OUT_OF_MEMORY;
	if (OwnedEntryAdd (entries, 0, "stereo.wav", wav, 44 + data))
		err = write_owned_entries (arg, basedir, depth, "KNON", entries, 1);
	else
		FREE (entries);
	FREE (wav);
	FREE (raw);
	return err;
}

// Paon "KNON"/"KWBK" wave bank (.WVD: Donkey Kong Barrel Blast sound effects): after the
// KWBK/3DWB headers, a WINF chunk lists u16 count @+8 and 0x60-byte entries {u16 index, u16 flags,
// u32 rate, u32 0, u32 size, ...} @+0x10; the WAVE chunk behind it (at WINF+8+size) holds the waves
// back to back from +0x10, each a standard mono DSP header + ADPCM padded to its entry size.
static enumError extract_knonwvd_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t wi = 0;
	if (sz > 0x200 && !memcmp (raw, "KNON", 4) && !memcmp (raw + 8, "WII ", 4) && !memcmp (raw + 0x20, "KWBK", 4))
		for (size_t p = 0x20; p + 0x10 < sz && p < 0x400; p += 4)
			if (!memcmp (raw + p, "WINF", 4))
			{
				wi = p;
				break;
			}
	const size_t wave = wi ? wi + 8 + rd_be32 (raw + wi + 4) : 0;
	const uint count = wi ? rd_be16 (raw + wi + 8) : 0;
	if (!wi || !count || wave + 0x10 > sz || memcmp (raw + wave, "WAVE", 4) || wi + 0x10 + (size_t)count * 0x60 > wave)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (count, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	size_t p = wave + 0x10;
	for (; n < count; n++)
	{
		const u8 *e = raw + wi + 0x10 + (size_t)n * 0x60;
		const size_t len = rd_be32 (e + 12);
		// looped waves keep their loop end in the nibble count, the sample count covers the tail
		size_t nib = rd_be32 (raw + p + 4);
		const size_t frames_ns = ((size_t)rd_be32 (raw + p) + 13) / 14;
		if (frames_ns * 16 > nib && frames_ns * 8 + 0x60 <= len + 1)
			nib = frames_ns * 16;
		const size_t per = ((nib + 15) / 16) * 8;
		if (len < 0x60 || p + len > sz || (nib + 1) / 2 + 0x60 > len + 1 || rd_be32 (raw + p + 8) != rd_be32 (e + 4))
			break;
		u8 *out = CALLOC (1, 0x60 + per); // the last frame may be cut short by the entry size
		if (!out)
			break;
		memcpy (out, raw + p, 0x60 + per > len ? len : 0x60 + per);
		wr_be32 (out + 4, (u32)nib);
		char name[24];
		snprintf (name, sizeof (name), "%04u.dsp", n);
		const bool added = OwnedEntryAdd (entries, n, name, out, 0x60 + per);
		FREE (out);
		if (!added)
			break;
		p += len;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (n == count)
		err = write_owned_entries (arg, basedir, depth, "KNON-WVD", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
