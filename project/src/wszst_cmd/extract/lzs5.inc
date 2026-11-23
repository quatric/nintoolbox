// "LZS\0" version 5 compressed files (Moki Moki, WiiWare: ".pacz"), big-endian. Header: {char "LZS\0", u8 version 5, u8 offset shift S, u8 unit size in bits (8, 16 or 32), u8 ?, u32 offset of the flag/token stream,
// u32 offset of the literal stream, u32 unpacked size in bytes, u32 file size, u8 chained-part flag (0), u8 minimum match length, ...}. Decoding works in units of 1, 2 or 4 bytes: the flag/token stream is a list of u16 words;
// a word read as flags gives 16 flags, lowest bit first: 1 copies one unit from the literal stream, 0 reads the next u16 word as a match token {offset in units = token >> S, length = (token & (2^S - 1)) + minimum} that copies
// from that many units back in the output (overlapping copies repeat). Ported from the game's own decoder; a header with the chained-part flag set is not handled. Written as "<file>.d/<name without .pacz>.bin" (an "add\0" pack).
static enumError extract_lzs5_file (ccp arg, ccp basedir, uint depth)
{
	u8 magic[5];
	FILE *f = fopen (arg, "rb");
	const bool hit = f && fread (magic, 1, 5, f) == 5 && !memcmp (magic, "LZS\0\x05", 5);
	if (f)
		fclose (f);
	if (!hit)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *out = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = 0;
	enumError err = ERR_NOTHING_TO_DO;
	const uint shift = sz >= 0x20 ? raw[5] : 0, unit = sz >= 0x20 ? raw[6] / 8 : 0, minlen = sz >= 0x20 ? raw[0x19] : 0;
	const u64 fo = sz >= 0x20 ? rd_be32 (raw + 8) : 0, lo = sz >= 0x20 ? rd_be32 (raw + 12) : 0, us = sz >= 0x20 ? rd_be32 (raw + 16) : 0;
	if (shift < 1 || shift > 15 || (unit != 1 && unit != 2 && unit != 4) || raw[0x18] || !us || us > 0x40000000 || us % unit || fo < 0x1c || fo > sz || lo > sz)
		goto done;
	out = MALLOC (us);
	if (!out)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	{
		const u32 mask = (1u << shift) - 1;
		u64 F = fo, L = lo, on = 0; // output position in bytes
		bool bad = false;
		while (on < us && !bad)
		{
			if (F + 2 > sz)
			{
				bad = true;
				break;
			}
			uint fl = rd_be16 (raw + F);
			F += 2;
			for (uint k = 0; k < 16 && on < us; k++, fl >>= 1)
			{
				if (fl & 1)
				{
					if (L + unit > sz)
					{
						bad = true;
						break;
					}
					memcpy (out + on, raw + L, unit);
					L += unit;
					on += unit;
				}
				else
				{
					if (F + 2 > sz)
					{
						bad = true;
						break;
					}
					const uint t = rd_be16 (raw + F);
					F += 2;
					const u64 off = (u64)(t >> shift) * unit, n = (u64)((t & mask) + minlen) * unit;
					if (!off || off > on)
					{
						bad = true;
						break;
					}
					for (u64 i = 0; i < n && on < us; i++, on++)
						out[on] = out[on - off];
				}
			}
		}
		if (bad || on != us)
			goto done;
	}
	entries = CALLOC (1, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	{
		const char *b = strrchr (arg, '/');
		char name[PATH_MAX];
		snprintf (name, sizeof (name), "%s", b ? b + 1 : arg);
		char *e = strrchr (name, '.');
		if (e && !strcasecmp (e, ".pacz"))
			*e = 0;
		const size_t l = strlen (name);
		snprintf (name + l, sizeof (name) - l, ".bin");
		if (us <= UINT_MAX && OwnedEntryAdd (entries, 0, name, out, (uint)us))
		{
			err = write_owned_entries (arg, basedir, depth, "LZS", entries, 1);
			entries = 0;
		}
	}
done:
	if (entries)
		ResetOwnedEntries (entries, 1);
	FREE (out);
	FREE (raw);
	return err;
}
