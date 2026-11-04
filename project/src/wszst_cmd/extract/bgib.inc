// "BGIB" level/sound packs (.gcs/.gcm, also embedded in .pak): Pheasants Forever, Tournament of Legends,
// Nickelodeon Dora/Diego/Kai-Lan/Fit, Rudolph, Pirates PlundArrr, The Secret Saturdays (Wii).
// Layout (big-endian header, 0x1f0 bytes, payload at 0x200):
//   +0x00 "BGIB", +0x04 header size 0x1f0, +0x10 pack name, +0x50 level name,
//   +0x80 size of the raw DSP-ADPCM blob stored at the very end of the file (0 if none),
//   +0x84 decompressed size, +0x8c compressed size, payload @0x200.
// The payload is the engine's byte-oriented LZ (decoder FUN_80019ddc in main.dol):
//   c & 0x80  : match, word = (c << 8 | b1) as int16, source = out + (word >> 4) - 1, length (b1 & 15) + 2
//   c < 0x40  : literal run of c + 1 bytes
//   otherwise : byte run, next byte repeated c - 0x3e times
// The decompressed stream starts with 32-byte aligned little-endian RIFF/"WAVS" sound records
// (guid, name, fmt, fact = samples, ngcc = 16 big-endian DSP coefficients, strm = {offset, size} into
// the tail blob; stereo records hold channel 0 then channel 1 as two consecutive halves, with two ngcc sets).
// Each channel becomes a standard "NNNN_<name>[_chN].dsp"; the whole stream is kept as "decompressed.bin".
static bool bgib_unlz (const u8 *src, size_t ssz, u8 *dst, size_t dsz)
{
	size_t i = 0, o = 0;
	while (i < ssz && o < dsz)
	{
		const uint c = src[i];
		if (c & 0x80)
		{
			if (i + 1 >= ssz)
				return false;
			const uint b1 = src[i + 1];
			i += 2;
			const int word = (int16_t)((c << 8) | b1);
			long p = (long)o + (word >> 4) - 1;
			for (uint n = (b1 & 15) + 2; n > 0 && o < dsz; n--, p++)
				dst[o++] = p >= 0 ? dst[p] : 0;
		}
		else if (c < 0x40)
		{
			size_t n = c + 1;
			if (i + 1 + n > ssz)
				return false;
			if (n > dsz - o)
				n = dsz - o;
			memcpy (dst + o, src + i + 1, n);
			o += n;
			i += 2 + c;
		}
		else
		{
			if (i + 1 >= ssz)
				return false;
			size_t n = c - 0x3e;
			if (n > dsz - o)
				n = dsz - o;
			memset (dst + o, src[i + 1], n);
			o += n;
			i += 2;
		}
	}
	return o == dsz;
}

static enumError extract_bgib_file (ccp arg, ccp basedir, uint depth)
{
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 mg[4] = {0};
	const bool is_bgib = fread (mg, 1, 4, f) == 4 && !memcmp (mg, "BGIB", 4);
	fclose (f);
	if (!is_bgib)
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0, *dec = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x200 || rd_be32 (raw + 4) != 0x1f0)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t blob_sz = rd_be32 (raw + 0x80), dec_sz = rd_be32 (raw + 0x84), cmp_sz = rd_be32 (raw + 0x8c);
	if (!dec_sz || dec_sz > 0x40000000 || !cmp_sz || cmp_sz > sz - 0x200 || blob_sz > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	dec = MALLOC (dec_sz);
	if (!dec || !bgib_unlz (raw + 0x200, cmp_sz, dec, dec_sz))
	{
		FREE (dec);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u8 *blob = blob_sz ? raw + sz - blob_sz : 0;

	nintendo_sarc_entry_t *entries = CALLOC (2 + 2 * (dec_sz / 0x80), sizeof (*entries));
	uint n = 0;
	bool good = entries && OwnedEntryAdd (entries, n, "decompressed.bin", dec, (uint)dec_sz);
	if (good)
		n++;

	size_t p = 0;
	uint idx = 0;
	while (good && blob && p + 12 <= dec_sz && !memcmp (dec + p, "RIFF", 4))
	{
		const size_t rsz = rd_le32 (dec + p + 4);
		if (p + 8 + rsz > dec_sz || rsz < 4)
			break;
		const size_t rend = p + 8 + rsz;
		if (!memcmp (dec + p + 8, "WAVS", 4))
		{
			const u8 *fmt = 0, *nm = 0, *cf = 0, *st = 0;
			uint fact = 0;
			size_t nm_len = 0, cf_len = 0;
			for (size_t q = p + 12; q + 8 <= rend;)
			{
				const size_t cs = rd_le32 (dec + q + 4);
				if (q + 8 + cs > rend)
					break;
				const u8 *cd = dec + q + 8;
				if (!memcmp (dec + q, "fmt ", 4) && cs >= 8)
					fmt = cd;
				else if (!memcmp (dec + q, "name", 4))
					nm = cd, nm_len = cs;
				else if (!memcmp (dec + q, "fact", 4) && cs >= 4)
					fact = rd_le32 (cd);
				else if (!memcmp (dec + q, "ngcc", 4) && cs >= 32)
					cf = cd, cf_len = cs;
				else if (!memcmp (dec + q, "strm", 4) && cs >= 8)
					st = cd;
				q += 8 + cs + (cs & 1);
			}
			const uint nch = fmt ? rd_le16 (fmt + 2) : 0;
			if (fmt && cf && st && fact && rd_le16 (fmt) == 0x666 && (nch == 1 || nch == 2)
				&& cf_len >= 0x30 * (nch - 1) + 32)
			{
				const size_t so = rd_le32 (st), ss = rd_le32 (st + 4), per_ch = ss / nch;
				const uint rate = rd_le32 (fmt + 4);
				const size_t frames = ((size_t)fact + 13) / 14;
				char base[64] = "sound";
				if (nm)
				{
					size_t k = 0;
					for (; k < nm_len && nm[k] && k < 48; k++)
						base[k] = (isalnum (nm[k]) || nm[k] == '_' || nm[k] == '-') ? nm[k] : '_';
					if (k > 4 && !strncasecmp (base + k - 4, "_wav", 4))
						k -= 4;
					if (k)
						base[k] = 0;
				}
				// channels are stored one after another, each with its own 0x30-byte coefficient set
				if (so + ss <= blob_sz && frames * 8 <= per_ch && rate)
					for (uint c = 0; c < nch && good; c++)
					{
						u8 *out = CALLOC (1, 0x60 + frames * 8);
						if (!out)
						{
							good = false;
							break;
						}
						wr_be32 (out, fact);
						wr_be32 (out + 4, (u32)(frames * 16));
						wr_be32 (out + 8, rate);
						wr_be32 (out + 0x10, 2);
						wr_be32 (out + 0x14, (fact - 1) / 14 * 16 + 2 + (fact - 1) % 14);
						wr_be32 (out + 0x18, 2);
						memcpy (out + 0x1c, cf + 0x30 * c, 32);
						memcpy (out + 0x60, blob + so + per_ch * c, frames * 8);
						out[0x3f] = out[0x60];
						char name[112];
						if (nch == 1)
							snprintf (name, sizeof (name), "%04u_%s.dsp", idx, base);
						else
							snprintf (name, sizeof (name), "%04u_%s_ch%u.dsp", idx, base, c);
						good = OwnedEntryAdd (entries, n, name, out, (uint)(0x60 + frames * 8));
						FREE (out);
						if (good)
							n++;
					}
			}
			idx++;
		}
		p = (rend + 31) & ~(size_t)31;
	}

	enumError err = ERR_NOTHING_TO_DO;
	if (good && n)
		err = write_owned_entries (arg, basedir, depth, "BGIB", entries, n);
	else
		FREE (entries);
	FREE (dec);
	FREE (raw);
	return err;
}
