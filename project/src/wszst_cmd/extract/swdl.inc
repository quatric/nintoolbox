// "swdl" (".swd", DSE / Procyon Studio sound banks, about 90 DS files; the ".sed"/"sedl" sequence files are left alone): header 0x40 bytes (0x50 in version 0x415 files), then chunks {tag, u16 0, u16 version, u32 0x10, u32 length} "wavi" (u16 pointer table at +0x10 to 0x40 byte sample entries), "prgi", "kgrp", "pcmd" (the sample data), "eod ". Entry: u16 id at +2, u16 format at +0x12 (0 PCM8, 0x100 PCM16, 0x200 4 bit IMA ADPCM), u32 sample rate at +0x20, u32 byte offset into the pcmd data at +0x24, u32 loop start (words) at +0x28, u32 loop length (words) at +0x2c; the stored size is 4 * (loop start + loop length) bytes (checked: matches the distance to the next entry). ADPCM: first word is {s16 predictor, u8 step index, 0}, then low nibble first IMA ADPCM (as in SWAV).
// Each sample is written as "<file>.d/<name>_<id>.wav" (mono 16 bit). Banks whose pcmd chunk is empty (samples elsewhere) give nothing. The decoder is the standard one; not compared against a reference by ear.
static const s16 swdl_step[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
static const s8 swdl_idx[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

static u8 *swdl_wav (const u8 *d, size_t n, uint fmt, uint rate, size_t *outn)
{
	size_t ns = fmt == 0 ? n : fmt == 0x100 ? n / 2 : n > 4 ? (n - 4) * 2 : 0;
	if (!ns || !rate)
		return 0;
	u8 *w = CALLOC (1, 44 + ns * 2);
	if (!w)
		return 0;
	memcpy (w, "RIFF", 4);
	wr_le32 (w + 4, 36 + (u32)ns * 2);
	memcpy (w + 8, "WAVEfmt ", 8);
	wr_le32 (w + 16, 16);
	wr_le16 (w + 20, 1);
	wr_le16 (w + 22, 1);
	wr_le32 (w + 24, rate);
	wr_le32 (w + 28, rate * 2);
	wr_le16 (w + 32, 2);
	wr_le16 (w + 34, 16);
	memcpy (w + 36, "data", 4);
	wr_le32 (w + 40, (u32)ns * 2);
	int pred = (s16)rd_le16 (d), si = d[2] > 88 ? 88 : d[2];
	for (size_t i = 0; i < ns; i++)
	{
		int v;
		if (fmt == 0)
			v = (int)(d[i] ^ 0x80) * 256 - 32768;
		else if (fmt == 0x100)
			v = (s16)rd_le16 (d + 2 * i);
		else
		{
			const int nb = i & 1 ? d[4 + i / 2] >> 4 : d[4 + i / 2] & 15, st = swdl_step[si];
			int df = st >> 3;
			if (nb & 4)
				df += st;
			if (nb & 2)
				df += st >> 1;
			if (nb & 1)
				df += st >> 2;
			pred += nb & 8 ? -df : df;
			pred = pred > 32767 ? 32767 : pred < -32768 ? -32768 : pred;
			si += swdl_idx[nb & 7];
			si = si < 0 ? 0 : si > 88 ? 88 : si;
			v = pred;
		}
		wr_le16 (w + 44 + 2 * i, (u16)v);
	}
	*outn = 44 + ns * 2;
	return w;
}

static enumError extract_swdl_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	nintendo_sarc_entry_t *entries = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		goto done;
	if (sz < 0x60 || memcmp (raw, "swdl", 4))
		goto done;
	size_t wavi = 0, pcmd = 0, pn = 0;
	for (size_t o = memcmp (raw + 0x40, "wavi", 4) ? 0x50 : 0x40; o + 16 <= sz;)
	{
		const size_t ln = rd_le32 (raw + o + 12);
		if (!memcmp (raw + o, "wavi", 4))
			wavi = o;
		else if (!memcmp (raw + o, "pcmd", 4))
			pcmd = o + 16, pn = ln;
		else if (!memcmp (raw + o, "eod ", 4))
			break;
		if (ln > sz || o + 16 + ln > sz)
		{
			if (!memcmp (raw + o, "pcmd", 4) && o + 16 < sz)
				pn = sz - o - 16; // pcmd length 0xaaaa0000 style placeholders
			break;
		}
		o = (o + 16 + ln + 15) & ~(size_t)15; // chunks are 16 byte aligned
	}
	if (!wavi || !pcmd || !pn)
		goto done;
	if (pcmd + pn > sz)
		pn = sz - pcmd;
	const size_t wl = rd_le32 (raw + wavi + 12);
	uint max = (uint)(wl / 64) + 1, k = 0;
	entries = CALLOC (max, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	const char *s = strrchr (arg, '/');
	for (uint i = 0; i < max && wavi + 16 + 2 * i + 2 <= sz; i++)
	{
		const size_t p = rd_le16 (raw + wavi + 16 + 2 * i);
		if (!p || p + 0x30 > wl)
			continue;
		const u8 *e = raw + wavi + 16 + p;
		const uint fmt = rd_le16 (e + 0x12), rate = rd_le32 (e + 0x20);
		const size_t off = rd_le32 (e + 0x24), len = 4 * ((size_t)rd_le32 (e + 0x28) + rd_le32 (e + 0x2c));
		if (off + len > pn || len < 8 || (fmt != 0 && fmt != 0x100 && fmt != 0x200))
			continue;
		size_t wn;
		u8 *w = swdl_wav (raw + pcmd + off, len, fmt, rate, &wn);
		char name[PATH_MAX];
		snprintf (name, sizeof (name), "%s_%u.wav", s ? s + 1 : arg, rd_le16 (e + 2));
		if (w && OwnedEntryAdd (entries, k, name, w, (uint)wn))
			k++;
		FREE (w);
	}
	if (k)
		err = write_owned_entries (arg, basedir, depth, "SWDL", entries, k);
	else
		ResetOwnedEntries (entries, max);
done:
	FREE (raw);
	return err;
}
