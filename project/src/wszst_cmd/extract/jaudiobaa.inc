// Nintendo JAudio "BAA" sound archives (".baa", "AA_<"; Twilight Princess Z2Sound.baa and other JAudio2 games) with their
// ".aw" wave files. Layout (big-endian), worked out from the Wii Twilight Princess disc:
//   "AA_<", then chunk records until ">_AA": "bst " / "bstn" / "bsc " {start, end}, "bfca" {start}, "bnk " {wsys id, offset},
//   "ws  " {id, offset, 0}.
//   A "ws  " chunk is a WSYS {"WSYS", size, id, u32 +0x0c, u32 WINF offset @+0x10, u32 WBCT offset @+0x14}; WINF {"WINF",
//   count, count group offsets}, each group {char name[0x70] (the ".aw" file), u32 waves, waves offsets}, all relative to
//   the WSYS. A wave record is 0x2c bytes {u8 0xcc, u8 format (0 = AFC 4 bit, 1 = AFC 2 bit, 2 = PCM8, 3 = PCM16), u8 key,
//   u8 pad, f32 rate, u32 offset in the .aw, u32 size, u32 loop flag (-1 = loops), u32 loop start, u32 loop end,
//   u32 samples, ...}. AFC is the 9 byte / 16 sample (or 5 byte / 16 sample) predictor ADPCM of vgmstream's afc_decoder.
// Every wave found in an ".aw" next to the archive (same folder, "Waves/" or "../Waves/") is written as
// "wsys<id>/<aw name>/<nnnn>.wav"; "bnk " / "bsc " / "bfca" chunks are written as raw "bnk<n>.bnk" etc.
static const int jaudio_afc_coefs[16][2] = { { 0, 0 }, { 2048, 0 }, { 0, 2048 }, { 1024, 1024 }, { 4096, -2048 },
	{ 3584, -1536 }, { 3072, -1024 }, { 4608, -2560 }, { 4200, -2248 }, { 4800, -2300 }, { 5120, -3072 },
	{ 2048, -2048 }, { 1024, -1024 }, { -1024, 1024 }, { -1024, 0 }, { -2048, 0 } };

static size_t jaudio_decode_afc (const u8 *d, size_t size, uint fmt, s16 *out, size_t max)
{
	const uint fb = fmt == 0 ? 9 : 5, per = 16;
	int h1 = 0, h2 = 0;
	size_t n = 0;
	for (size_t f = 0; (f + 1) * fb <= size && n + per <= max + per - 1 && n < max; f++)
	{
		const u8 *fr = d + f * fb;
		const int scale = fmt == 0 ? 1 << (fr[0] >> 4) : 8192 << (fr[0] >> 4);
		const int c1 = jaudio_afc_coefs[fr[0] & 15][0], c2 = jaudio_afc_coefs[fr[0] & 15][1];
		for (uint i = 0; i < per && n < max; i++)
		{
			int code;
			if (fmt == 0)
			{
				code = (fr[1 + i / 2] >> ((i & 1) ? 0 : 4)) & 15;
				if (code >= 8)
					code -= 16;
			}
			else
			{
				static const int two[4] = { 0, 1, -2, -1 };
				code = two[(fr[1 + i / 4] >> (6 - 2 * (i & 3))) & 3];
			}
			int v = fmt == 0 ? (code * scale) << 11 : code * scale;
			v = (v + c1 * h1 + c2 * h2) >> 11;
			if (v > 32767)
				v = 32767;
			if (v < -32768)
				v = -32768;
			out[n++] = (s16)v;
			h2 = h1;
			h1 = v;
		}
	}
	return n;
}

static bool jaudio_find_aw (ccp baa, ccp name, char *path, size_t psize)
{
	char dir[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s", baa);
	char *sl = strrchr (dir, '/');
	if (sl)
		sl[1] = 0;
	else
		dir[0] = 0;
	static ccp const where[] = { "", "Waves/", "../Waves/", "waves/", "../waves/" };
	for (uint i = 0; i < sizeof (where) / sizeof (*where); i++)
	{
		snprintf (path, psize, "%s%s%s", dir, where[i], name);
		FILE *f = fopen (path, "rb");
		if (f)
		{
			fclose (f);
			return true;
		}
	}
	return false;
}

static u8 *jaudio_make_wav (const s16 *pcm, size_t frames, uint rate, size_t *out_size)
{
	const size_t dsz = frames * 2;
	u8 *wav = MALLOC (44 + dsz);
	if (!wav)
		return 0;
	memcpy (wav, "RIFF", 4);
	wr_le32 (wav + 4, (u32)(36 + dsz));
	memcpy (wav + 8, "WAVEfmt ", 8);
	wr_le32 (wav + 16, 16);
	wr_le16 (wav + 20, 1);
	wr_le16 (wav + 22, 1);
	wr_le32 (wav + 24, rate);
	wr_le32 (wav + 28, rate * 2);
	wr_le16 (wav + 32, 2);
	wr_le16 (wav + 34, 16);
	memcpy (wav + 36, "data", 4);
	wr_le32 (wav + 40, (u32)dsz);
	for (size_t i = 0; i < frames; i++)
		wr_le16 (wav + 44 + 2 * i, (u16)pcm[i]);
	*out_size = 44 + dsz;
	return wav;
}

static enumError extract_jaudiobaa_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".baa"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x20 || memcmp (raw, "AA_<", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	// chunk records
	typedef struct { u32 tag, a, b; } rec_t;
	rec_t recs[256];
	uint nrec = 0;
	size_t p = 4;
	bool end = false;
	while (p + 4 <= sz && nrec < 256)
	{
		const u32 tag = rd_be32 (raw + p);
		if (tag == 0x3e5f4141) // ">_AA"
		{
			end = true;
			break;
		}
		uint words;
		if (tag == 0x62737420 || tag == 0x6273746e || tag == 0x62736320) // "bst " "bstn" "bsc "
			words = 2;
		else if (tag == 0x62666361) // "bfca"
			words = 1;
		else if (tag == 0x77732020) // "ws  "
			words = 3;
		else if (tag == 0x626e6b20) // "bnk "
			words = 2;
		else
			break;
		if (p + 4 + 4 * words > sz)
			break;
		recs[nrec].tag = tag;
		recs[nrec].a = rd_be32 (raw + p + 4);
		recs[nrec].a = rd_be32 (raw + p + 4); // "ws  ": id, "bnk ": wsys id, "bst " / "bsc " / "bfca": start
		recs[nrec].b = words >= 2 ? rd_be32 (raw + p + 8) : 0; // offset / end
		nrec++;
		p += 4 + 4 * words;
	}
	if (!end || !nrec)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	size_t cap = 64, n = 0;
	nintendo_sarc_entry_t *entries = CALLOC (cap, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint nws = 0, nbnk = 0, nbsc = 0;
	for (uint r = 0; r < nrec; r++)
	{
		char name[96];
		if (recs[r].tag == 0x626e6b20 || recs[r].tag == 0x62736320 || recs[r].tag == 0x62666361)
		{
			// raw chunks: "bnk " {id, offset} runs to the next recorded offset, "bsc " {start, end}, "bfca" {start, file end}
			size_t start, stop;
			if (recs[r].tag == 0x626e6b20)
			{
				start = recs[r].b;
				stop = sz;
				for (uint k = 0; k < nrec; k++)
				{
					const size_t o = (recs[k].tag == 0x626e6b20 || recs[k].tag == 0x77732020) ? recs[k].b : recs[k].a;
					if (o > start && o < stop)
						stop = o;
				}
				snprintf (name, sizeof (name), "bnk%u_%u.bnk", nbnk++, recs[r].a);
			}
			else
			{
				start = recs[r].a;
				stop = recs[r].tag == 0x62666361 ? sz : recs[r].b;
				snprintf (name, sizeof (name), "%s%u.%s", recs[r].tag == 0x62736320 ? "bsc" : "bfca",
					nbsc++, recs[r].tag == 0x62736320 ? "bsc" : "bfca");
			}
			if (start >= sz || stop > sz || stop <= start || n + 1 >= cap)
				continue;
			if (OwnedEntryAdd (entries, n, name, raw + start, stop - start))
				n++;
			continue;
		}
		if (recs[r].tag != 0x77732020)
			continue;
		nws++;
		const size_t w = recs[r].b;
		if (w + 0x18 > sz || memcmp (raw + w, "WSYS", 4))
			continue;
		const size_t winf = w + rd_be32 (raw + w + 0x10);
		if (winf + 8 > sz || memcmp (raw + winf, "WINF", 4))
			continue;
		const uint groups = rd_be32 (raw + winf + 4);
		if (groups > 4096 || winf + 8 + 4 * (size_t)groups > sz)
			continue;
		for (uint g = 0; g < groups; g++)
		{
			const size_t go = w + rd_be32 (raw + winf + 8 + 4 * (size_t)g);
			if (go + 0x74 > sz)
				continue;
			char awname[0x71];
			memcpy (awname, raw + go, 0x70);
			awname[0x70] = 0;
			const uint nw = rd_be32 (raw + go + 0x70);
			if (!awname[0] || strchr (awname, '/') || nw > 100000 || go + 0x74 + 4 * (size_t)nw > sz)
				continue;
			char awpath[PATH_MAX];
			if (!jaudio_find_aw (arg, awname, awpath, sizeof (awpath)))
				continue;
			u8 *aw = 0;
			size_t aws = 0;
			if (LoadFileAlloc (awpath, 0, 0, &aw, &aws, 0, 0, 0, false) || !aw)
			{
				FREE (aw);
				continue;
			}
			char stem[0x71];
			snprintf (stem, sizeof (stem), "%s", awname);
			char *dot = strrchr (stem, '.');
			if (dot)
				*dot = 0;
			for (uint k = 0; k < nw; k++)
			{
				const size_t eo = w + rd_be32 (raw + go + 0x74 + 4 * (size_t)k);
				if (eo + 0x20 > sz)
					continue;
				const uint fmt = raw[eo + 1];
				u32 rate_bits = rd_be32 (raw + eo + 4);
				float rate_f;
				memcpy (&rate_f, &rate_bits, 4);
				const u32 aoff = rd_be32 (raw + eo + 8), asize = rd_be32 (raw + eo + 0xc), samples = rd_be32 (raw + eo + 0x1c);
				if (rate_f < 1000 || rate_f > 200000 || aoff > aws || asize > aws - aoff || !samples || samples > 0x4000000)
					continue;
				s16 *pcm = MALLOC ((size_t)samples * 2 + 64);
				size_t frames = 0;
				if (!pcm)
					continue;
				if (fmt == 0 || fmt == 1)
					frames = jaudio_decode_afc (aw + aoff, asize, fmt, pcm, samples);
				else if (fmt == 2)
					for (; frames < asize && frames < samples; frames++)
						pcm[frames] = (s16)((int8_t)aw[aoff + frames] * 256);
				else if (fmt == 3)
					for (; frames * 2 + 2 <= asize && frames < samples; frames++)
						pcm[frames] = (s16)rd_be16 (aw + aoff + frames * 2);
				size_t wsize = 0;
				u8 *wav = frames ? jaudio_make_wav (pcm, frames, (uint)(rate_f + 0.5f), &wsize) : 0;
				FREE (pcm);
				if (!wav)
					continue;
				if (n + 1 >= cap)
				{
					cap *= 2;
					nintendo_sarc_entry_t *ne = REALLOC (entries, cap * sizeof (*entries));
					if (!ne)
					{
						FREE (wav);
						break;
					}
					memset (ne + cap / 2, 0, cap / 2 * sizeof (*ne));
					entries = ne;
				}
				snprintf (name, sizeof (name), "wsys%u/%s/%04u.wav", recs[r].a, stem, k);
				if (OwnedEntryAdd (entries, n, name, wav, wsize))
					n++;
				FREE (wav);
			}
			FREE (aw);
		}
	}
	FREE (raw);
	if (!n)
	{
		FREE (entries);
		return ERR_NOTHING_TO_DO;
	}
	return write_owned_entries (arg, basedir, depth, "JAUDIO-BAA", entries, n);
}
