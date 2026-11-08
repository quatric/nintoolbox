// Downloadable Sounds banks (".dls", RIFF "DLS "; the Roland GM16.DLS general MIDI bank that a family of budget Wii
// games ship in sound/Roland, 495 mono 16 bit 22.05 kHz waves): every entry of the "LIST wvpl" wave pool is a
// "LIST wave" with a "fmt " and a "data" chunk and an optional INFO/INAM name. Each wave is written as a plain WAV
// "<index>[_<name>].wav" (the fmt chunk is copied verbatim, so non PCM codecs stay as they are) into "<file>.d".
// Instrument, region and articulation data (lins, ptbl) are not exported.
static enumError extract_dls_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".dls"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 20 || memcmp (raw, "RIFF", 4) || memcmp (raw + 8, "DLS ", 4))
		goto done;
	size_t end = 8 + (size_t)rd_le32 (raw + 4);
	if (end > sz)
		end = sz;
	size_t pool = 0, pool_end = 0;
	for (size_t o = 12; o + 12 <= end;)
	{
		const size_t n = rd_le32 (raw + o + 4);
		if (n > end - o - 8)
			goto done;
		if (!memcmp (raw + o, "LIST", 4) && !memcmp (raw + o + 8, "wvpl", 4))
		{
			pool = o + 12;
			pool_end = o + 8 + n;
			break;
		}
		o += 8 + n + (n & 1);
	}
	if (!pool)
		goto done;

	uint count = 0;
	for (size_t o = pool; o + 12 <= pool_end;)
	{
		const size_t n = rd_le32 (raw + o + 4);
		if (n > pool_end - o - 8)
			goto done;
		if (!memcmp (raw + o, "LIST", 4) && !memcmp (raw + o + 8, "wave", 4))
			count++;
		o += 8 + n + (n & 1);
	}
	if (!count || count > 0x10000)
		goto done;
	nintendo_sarc_entry_t *entries = CALLOC (count, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	uint k = 0;
	bool good = true;
	for (size_t o = pool; o + 12 <= pool_end && good;)
	{
		const size_t n = rd_le32 (raw + o + 4);
		if (!memcmp (raw + o, "LIST", 4) && !memcmp (raw + o + 8, "wave", 4))
		{
			const u8 *fmt = 0, *data = 0;
			uint fmtn = 0, datan = 0;
			char label[48] = "";
			for (size_t p = o + 12; p + 8 <= o + 8 + n;)
			{
				const size_t m = rd_le32 (raw + p + 4);
				if (m > o + 8 + n - p - 8)
					break;
				if (!memcmp (raw + p, "fmt ", 4))
					fmt = raw + p + 8, fmtn = m;
				else if (!memcmp (raw + p, "data", 4))
					data = raw + p + 8, datan = m;
				else if (!memcmp (raw + p, "LIST", 4) && m >= 4 && !memcmp (raw + p + 8, "INFO", 4))
					for (size_t q = p + 12; q + 8 <= p + 8 + m;)
					{
						const size_t l = rd_le32 (raw + q + 4);
						if (l > p + 8 + m - q - 8)
							break;
						if (!memcmp (raw + q, "INAM", 4))
						{
							uint j = 0;
							for (; j < l && j < sizeof (label) - 1 && raw[q + 8 + j]; j++)
							{
								const u8 c = raw[q + 8 + j];
								label[j] = (c >= 0x20 && c < 0x7f && c != '/' && c != '\\' && c != ':') ? (char)c : '_';
							}
							label[j] = 0;
						}
						q += 8 + l + (l & 1);
					}
				p += 8 + m + (m & 1);
			}
			if (!fmt || !data || fmtn < 14)
			{
				good = false;
				break;
			}
			const size_t total = 12 + 8 + fmtn + (fmtn & 1) + 8 + datan + (datan & 1);
			u8 *wav = MALLOC (total);
			if (!wav)
			{
				good = false;
				break;
			}
			u8 *w = wav;
			memcpy (w, "RIFF", 4);
			wr_le32 (w + 4, (uint)(total - 8));
			memcpy (w + 8, "WAVEfmt ", 8);
			wr_le32 (w + 16, fmtn);
			memcpy (w + 20, fmt, fmtn);
			w += 20 + fmtn;
			if (fmtn & 1)
				*w++ = 0;
			memcpy (w, "data", 4);
			wr_le32 (w + 4, datan);
			memcpy (w + 8, data, datan);
			w += 8 + datan;
			if (datan & 1)
				*w++ = 0;
			char name[96];
			if (*label)
				snprintf (name, sizeof (name), "%04u_%s.wav", k, label);
			else
				snprintf (name, sizeof (name), "%04u.wav", k);
			good = OwnedEntryAdd (entries, k, name, wav, (uint)total);
			FREE (wav);
			k++;
		}
		o += 8 + n + (n & 1);
	}
	if (good && k == count)
		err = write_owned_entries (arg, basedir, depth, "DLS", entries, count);
	else
	{
		ResetOwnedEntries (entries, count);
	}
done:
	FREE (raw);
	return err;
}
