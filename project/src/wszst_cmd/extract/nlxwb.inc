// Next Level Games wave bank (Mario Strikers Charged, Wii): a ".resbun" resource bundle plus a headerless
// ".nlxwb" file next to it that holds the raw DSP-ADPCM data of every sound. The bundle is a tree of
// big-endian {u32 tag, u32 size} chunks (tag bit 31 = container). Chunk 0x03023703 carries the wave table:
// u32 count (some banks put an extra 0 word in front of it), count 0x1c-byte entries {0, rate, 0, 0, end
// nibble address, start nibble address, 0}, then count 0x2e-byte blocks with the DSP coefficients and
// history {s16 coef[16], gain, initial scale, history 1, history 2, loop scale, loop history 1 / 2}.
// Addresses are nibble offsets into the ".nlxwb" file. Every sound is written as "<NN>.dsp" (decoded to
// WAV by the DSP extractor). Banks without that chunk are stream banks (see nlxwb_streams).
static bool nlxwb_find (const u8 *d, size_t sz, size_t p, size_t end, u32 want, size_t *tab, size_t *tsz)
{
	while (p + 8 <= end && end <= sz)
	{
		const u32 tag = rd_be32 (d + p), len = rd_be32 (d + p + 4);
		if (len > end - p - 8)
			return false;
		if (tag & 0x80000000)
		{
			if (nlxwb_find (d, sz, p + 8, p + 8 + len, want, tab, tsz))
				return true;
		}
		else if (tag == want)
		{
			*tab = p + 8;
			*tsz = len;
			return true;
		}
		p += 8 + (size_t)len;
	}
	return false;
}

// Stream banks (music / crowd / cut-scene audio): chunk 0x23202 lists count 28-byte entries {index, offset,
// size, hash, channels, 0, 0}; each stream is a complete "IDSP" file inside the ".nlxwb" and is written as
// "<NN>.idsp" for the IDSP extractor. Takes ownership of "raw".
static enumError nlxwb_streams (ccp arg, ccp basedir, uint depth, u8 *raw, size_t sz)
{
	u8 *wav = 0;
	size_t wsz = 0, tab = 0, tsz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	nintendo_sarc_entry_t *entries = 0;
	if (!nlxwb_find (raw, sz, 0, sz, 0x23202, &tab, &tsz) || !tsz || tsz % 28 || tsz / 28 > 4096)
		goto done;
	const uint count = tsz / 28;
	char sib[PATH_MAX];
	snprintf (sib, sizeof (sib), "%s", arg);
	strcpy (sib + strlen (sib) - 7, ".nlxwb");
	if (LoadFileAlloc (sib, 0, 0, &wav, &wsz, 0, 0, 0, false) || !wav)
		goto done;
	entries = CALLOC (count, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	uint n = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = raw + tab + (size_t)i * 28;
		const size_t off = rd_be32 (e + 4), size = rd_be32 (e + 8);
		if (rd_be32 (e) != i || off > wsz || size < 0x60 || size > wsz - off || memcmp (wav + off, "IDSP", 4))
			break;
		char name[16];
		snprintf (name, sizeof (name), "%02u.idsp", i);
		if (!OwnedEntryAdd (entries, n, name, wav + off, size))
			break;
		n++;
	}
	if (n == count)
	{
		err = write_owned_entries (arg, basedir, depth, "NLXWB-STREAM", entries, n);
		entries = 0;
	}
done:
	FREE (entries);
	FREE (wav);
	FREE (raw);
	return err;
}

static enumError extract_nlxwb_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".resbun"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *wav = 0;
	size_t sz = 0, wsz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	nintendo_sarc_entry_t *entries = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		goto done;
	size_t tab = 0, tsz = 0;
	if (sz < 0x20 || !(rd_be32 (raw) & 0x80000000) || !nlxwb_find (raw, sz, 0, sz, 0x03023703, &tab, &tsz) || tsz < 4)
		return nlxwb_streams (arg, basedir, depth, raw, sz);
	size_t p = tab;
	uint count = rd_be32 (raw + p);
	if (!count && tsz >= 8)
	{
		p += 4;
		count = rd_be32 (raw + p);
	}
	const size_t hdr = p - tab + 4;
	if (!count || count > 4096 || hdr + (size_t)count * 0x4a != tsz)
		goto done;
	char sib[PATH_MAX];
	snprintf (sib, sizeof (sib), "%s", arg);
	strcpy (sib + strlen (sib) - 7, ".nlxwb");
	if (LoadFileAlloc (sib, 0, 0, &wav, &wsz, 0, 0, 0, false) || !wav)
		goto done;
	const u8 *ent = raw + p + 4, *blk = ent + (size_t)count * 0x1c;
	entries = CALLOC (count, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	uint n = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = ent + (size_t)i * 0x1c, *b = blk + (size_t)i * 0x2e;
		const u32 rate = rd_be32 (e + 4), end = rd_be32 (e + 0x10), start = rd_be32 (e + 0x14);
		const u32 base = start & ~15u;
		if (!rate || rate > 96000 || (start & 15) != 2 || end < start)
			break;
		const size_t nib = (size_t)end - base + 1, bytes = (nib + 15) / 16 * 8, off = base / 2;
		if (off > wsz || (size_t)end / 2 >= wsz) // the last frame of the last sound may be cut short
			break;
		const size_t have = bytes > wsz - off ? wsz - off : bytes;
		u8 *out = CALLOC (1, 0x60 + bytes);
		if (!out)
			break;
		wr_be32 (out, (uint)(nib / 16 * 14 + (nib % 16 > 2 ? nib % 16 - 2 : 0)));
		wr_be32 (out + 4, (u32)nib);
		wr_be32 (out + 8, rate);
		wr_be32 (out + 0x10, 2);
		wr_be32 (out + 0x14, (u32)nib - 1);
		wr_be32 (out + 0x18, 2);
		memcpy (out + 0x1c, b, 32); // coefficients
		memcpy (out + 0x3c, b + 32, 14); // gain, scale, history, loop scale / history
		out[0x3f] = wav[off]; // initial scale = first frame header
		out[0x3e] = 0;
		memcpy (out + 0x60, wav + off, have);
		char name[16];
		snprintf (name, sizeof (name), "%02u.dsp", i);
		const bool added = OwnedEntryAdd (entries, n, name, out, 0x60 + bytes);
		FREE (out);
		if (!added)
			break;
		n++;
	}
	if (n == count)
	{
		err = write_owned_entries (arg, basedir, depth, "NLXWB", entries, n);
		entries = 0;
	}
done:
	FREE (entries);
	FREE (wav);
	FREE (raw);
	return err;
}
