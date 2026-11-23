// "PACK" files (".set", ".fnt", ".bin", ".def", ".ftb", ... of about 37 German-published DS games such as My Riding Stables, Bibi & Tina, Oktoberfest: The Official Game), little-endian: {char "PACK", u32 flags (0, sometimes 1), u32 unpacked size,
// u32 unpacked size again (or twice that), then one or more Nintendo LZ10 streams (type byte 0x10, each starting on a 4 byte boundary)}. The streams are decoded one after the other until the unpacked size is reached; bytes left over behind them
// are kept as "<name>.tail". PACK files with another layout (flags other than 0/1, such as the 25 of an ".fpk") are left alone. The unpacked data is written as "<file>.d/<name>.bin" and is not decoded further.
static bool packlz_lz10 (const u8 *src, size_t n, size_t *used, u8 **out, size_t *outsz)
{
	if (n < 5 || src[0] != 0x10)
		return false;
	const size_t us = src[1] | src[2] << 8 | src[3] << 16;
	if (!us)
		return false;
	u8 *o = MALLOC (us);
	if (!o)
		return false;
	size_t i = 4, op = 0;
	while (op < us)
	{
		if (i >= n)
			goto bad;
		const u8 flags = src[i++];
		for (uint b = 0; b < 8 && op < us; b++)
		{
			if (flags & (0x80 >> b))
			{
				if (i + 2 > n)
					goto bad;
				const uint len = (src[i] >> 4) + 3, back = ((src[i] & 15) << 8 | src[i + 1]) + 1;
				i += 2;
				if (back > op || len > us - op)
					goto bad;
				for (uint k = 0; k < len; k++, op++)
					o[op] = o[op - back];
			}
			else
			{
				if (i >= n)
					goto bad;
				o[op++] = src[i++];
			}
		}
	}
	*used = i;
	*out = o;
	*outsz = us;
	return true;
bad:
	FREE (o);
	return false;
}

static enumError extract_packlz_file (ccp arg, ccp basedir, uint depth)
{
	u8 magic[4];
	FILE *f = fopen (arg, "rb");
	const bool hit = f && fread (magic, 1, 4, f) == 4 && !memcmp (magic, "PACK", 4);
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
	if (sz < 21 || rd_le32 (raw + 4) > 1)
		goto done;
	const u64 us = rd_le32 (raw + 8), us2 = rd_le32 (raw + 12);
	if (!us || us > 0x20000000 || (us2 != us && us2 != 2 * us))
		goto done;
	out = MALLOC (us);
	if (!out)
		goto done;
	size_t p = 16, tot = 0;
	while (tot < us)
	{
		size_t used, n;
		u8 *part;
		if (p >= sz || !packlz_lz10 (raw + p, sz - p, &used, &part, &n) || n > us - tot)
			goto done;
		memcpy (out + tot, part, n);
		FREE (part);
		tot += n;
		p += (used + 3) & ~(size_t)3;
	}
	if (p > sz)
		p = sz;
	entries = CALLOC (2, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	{
		const char *s = strrchr (arg, '/');
		char name[PATH_MAX];
		snprintf (name, sizeof (name), "%s", s ? s + 1 : arg);
		const size_t l = strlen (name);
		uint n = 0;
		snprintf (name + l, sizeof (name) - l, ".bin");
		if (OwnedEntryAdd (entries, n, name, out, (uint)us))
			n++;
		if (sz - p > 4)
		{
			snprintf (name + l, sizeof (name) - l, ".tail");
			if (OwnedEntryAdd (entries, n, name, raw + p, (uint)(sz - p)))
				n++;
		}
		if (n)
		{
			err = write_owned_entries (arg, basedir, depth, "PACK", entries, n);
			entries = 0;
		}
	}
done:
	if (entries)
		ResetOwnedEntries (entries, 2);
	FREE (out);
	FREE (raw);
	return err;
}
