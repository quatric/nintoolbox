// Rockstar Table Tennis (Wii) string tables "pong/assets/strtbl/<region>/*.strtbl", little-endian: u32 block count n, n u32 offsets (languages; unused ones point at the
// end of the file), then a key block {u32 flags (0x200), u32 count, count * {u32 length, key, NUL}} that ends at the first offset, and per language a block
// {u32 count, count * {u32 hash, u16 1, u32 length, font tag, u32 chars, UTF-16LE text (with NUL), 10 bytes of zeros}} ending at the next offset. Written as "<file>.tsv"
// with the columns language block, entry index, hash, font and text (tab / newline escaped). The keys of the first block are only validated: the entries are sorted by a key hash that
// has not been identified, so the n-th key does not belong to the n-th text.
static void strtbl_utf8 (pcr_ctx_t *c, const u8 *s, uint n)
{
	for (uint i = 0; i < n; i++)
	{
		uint u = rd_le16 (s + 2 * i);
		if (!u)
			continue;
		if (u >= 0xd800 && u < 0xdc00 && i + 1 < n)
		{
			const uint lo = rd_le16 (s + 2 * i + 2);
			if (lo >= 0xdc00 && lo < 0xe000)
				u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00), i++;
		}
		if (u == '\t')
			pcr_emit (c, "\\t");
		else if (u == '\n')
			pcr_emit (c, "\\n");
		else if (u == '\r')
			pcr_emit (c, "\\r");
		else if (u == '\\')
			pcr_emit (c, "\\\\");
		else if (u < 0x80)
			pcr_emit (c, "%c", (int)u);
		else if (u < 0x800)
			pcr_emit (c, "%c%c", (int)(0xc0 | u >> 6), (int)(0x80 | (u & 0x3f)));
		else if (u < 0x10000)
			pcr_emit (c, "%c%c%c", (int)(0xe0 | u >> 12), (int)(0x80 | ((u >> 6) & 0x3f)), (int)(0x80 | (u & 0x3f)));
		else
			pcr_emit (c, "%c%c%c%c", (int)(0xf0 | u >> 18), (int)(0x80 | ((u >> 12) & 0x3f)), (int)(0x80 | ((u >> 6) & 0x3f)), (int)(0x80 | (u & 0x3f)));
	}
}

static enumError extract_strtbl_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".strtbl"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	enumError err = ERR_NOTHING_TO_DO;
	const u32 n = sz >= 16 ? rd_le32 (raw) : 0;
	if (!n || n > 64 || 4 + 4 * (size_t)n + 8 > sz)
		goto done;
	{
		u32 off[65];
		for (u32 i = 0; i < n; i++)
			off[i] = rd_le32 (raw + 4 + 4 * (size_t)i);
		off[n] = (u32)sz;
		const size_t ks = 4 + 4 * (size_t)n;
		const u32 kc = rd_le32 (raw + ks + 4);
		if (!kc || kc > 1000000 || off[0] > sz)
			goto done;
		const u8 **key = CALLOC (kc, sizeof (*key));
		uint *klen = CALLOC (kc, sizeof (*klen));
		if (!key || !klen)
		{
			FREE (key);
			FREE (klen);
			goto done;
		}
		size_t p = ks + 8;
		bool good = true;
		for (u32 i = 0; i < kc && good; i++)
		{
			if (p + 5 > sz)
				good = false;
			else
			{
				const u32 l = rd_le32 (raw + p);
				if (p + 5 + l > sz)
					good = false;
				else
					key[i] = raw + p + 4, klen[i] = l, p += 5 + (size_t)l;
			}
		}
		good = good && p == off[0];
		pcr_emit (&c, "language\tindex\thash\tfont\ttext\n");
		uint nl = 0;
		for (u32 b = 0; b < n && good; b++)
		{
			if (off[b] == off[b + 1] || b && off[b] == sz)
				continue;
			if (off[b] + 4 > sz || off[b] > off[b + 1])
			{
				good = false;
				break;
			}
			size_t q = off[b] + 4;
			if (rd_le32 (raw + off[b]) != kc)
			{
				good = false;
				break;
			}
			for (u32 i = 0; i < kc && good; i++)
			{
				if (q + 14 > off[b + 1])
				{
					good = false;
					break;
				}
				const u32 h = rd_le32 (raw + q), fl = rd_le32 (raw + q + 6);
				q += 10;
				if (q + fl + 4 > off[b + 1])
				{
					good = false;
					break;
				}
				const u8 *font = raw + q;
				q += fl;
				const u32 nch = rd_le32 (raw + q);
				q += 4;
				if (q + 2 * (size_t)nch + 10 > off[b + 1])
				{
					good = false;
					break;
				}
				pcr_emit (&c, "%u\t%u\t%08x\t%.*s\t", b, i, h, (int)fl, (ccp)font);
				strtbl_utf8 (&c, raw + q, nch);
				pcr_emit (&c, "\n");
				q += 2 * (size_t)nch + 10;
			}
			good = good && q == off[b + 1];
			nl++;
		}
		FREE (key);
		FREE (klen);
		if (good && nl && c.ok)
		{
			char dest[PATH_MAX];
			snprintf (dest, sizeof (dest), "%s.tsv", arg);
			if (testmode)
			{
				fprintf (stdlog, "WOULD EXTRACT STRTBL:%s -> TSV:%s\n", arg, dest);
				err = ERR_OK;
			}
			else
			{
				File_t F;
				err = CreateFileOpt (&F, true, dest, false, arg);
				if (F.f && fwrite (c.out, 1, c.len, F.f) != c.len)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)c.len, dest);
				ResetFile (&F, opt_preserve);
				if (verbose >= 0 && !err)
					fprintf (stdlog, "%sEXTRACT STRTBL:%s (%u languages) -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, nl, dest);
			}
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
