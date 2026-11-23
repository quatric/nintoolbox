// "RLE!" blocks (".til", ".msk" and others of about 40 DS games), little endian: {char "RLE!", u32 unpacked size, u32 packed size, u32 0, packed bytes}. A control byte below 0x80 copies control + 1 literal bytes, one with bit 7 set repeats
// the next byte (control & 0x7f) + 1 times. A file can hold several blocks one after the other (each followed by up to 3 zero padding bytes); every block must unpack to exactly its stored size. The unpacked blocks are written as
// "<file>.d/<name>.bin" (one block) or "<name>_<n>.bin"; bytes after the last block are kept as "<name>.tail".
static bool rlebang_block (const u8 *src, size_t n, u8 **out, size_t *outn)
{
	const size_t us = rd_le32 (src + 4), ps = rd_le32 (src + 8);
	if (n < 16 || ps > n - 16 || !us || us > 0x4000000 || rd_le32 (src + 12))
		return false;
	u8 *o = MALLOC (us);
	if (!o)
		return false;
	size_t i = 16, op = 0;
	while (i < 16 + ps && op < us)
	{
		const uint c = src[i++];
		if (c & 0x80)
		{
			const size_t len = (c & 0x7f) + 1;
			if (i >= 16 + ps || len > us - op)
				goto bad;
			memset (o + op, src[i++], len);
			op += len;
		}
		else
		{
			const size_t len = c + 1;
			if (i + len > 16 + ps || len > us - op)
				goto bad;
			memcpy (o + op, src + i, len);
			i += len;
			op += len;
		}
	}
	if (op != us || i != 16 + ps)
		goto bad;
	*out = o;
	*outn = 16 + ps;
	return true;
bad:
	FREE (o);
	return false;
}

static enumError extract_rlebang_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	nintendo_sarc_entry_t *entries = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 17 || memcmp (raw, "RLE!", 4))
		goto done;
	enum { MAXB = 4096 };
	entries = CALLOC (MAXB + 1, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	const char *sl = strrchr (arg, '/');
	char base[PATH_MAX], name[PATH_MAX + 16];
	snprintf (base, sizeof (base), "%s", sl ? sl + 1 : arg);
	size_t p = 0;
	uint nb = 0;
	while (nb < MAXB && p + 16 <= sz && !memcmp (raw + p, "RLE!", 4))
	{
		u8 *blk;
		size_t used;
		if (!rlebang_block (raw + p, sz - p, &blk, &used))
			break;
		snprintf (name, sizeof (name), "%s_%03u.bin", base, nb);
		const bool ok = OwnedEntryAdd (entries, nb, name, blk, (uint)rd_le32 (raw + p + 4));
		FREE (blk);
		if (!ok)
			break;
		nb++;
		p += used;
		for (uint z = 0; z < 3 && p < sz && !raw[p] && !(p + 4 <= sz && !memcmp (raw + p, "RLE!", 4)); z++)
			p++;
	}
	if (!nb)
		goto done;
	if (nb == 1)
	{
		snprintf (name, sizeof (name), "%s.bin", base);
		const char *old = entries[0].name;
		entries[0].name = MALLOC (strlen (name) + 1);
		if (entries[0].name)
			strcpy ((char *)entries[0].name, name);
		FREE ((char *)old);
	}
	uint total = nb;
	if (sz - p > 4)
	{
		snprintf (name, sizeof (name), "%s.tail", base);
		if (OwnedEntryAdd (entries, total, name, raw + p, (uint)(sz - p)))
			total++;
	}
	err = write_owned_entries (arg, basedir, depth, "RLE!", entries, total);
	entries = 0;
done:
	if (entries)
		ResetOwnedEntries (entries, MAXB + 1);
	FREE (raw);
	return err;
}
