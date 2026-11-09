// Momotarou Dentetsu 16 "cmps" archives (data/field.bin, object.bin, player.bin, boot.bin, event_new.bin; big endian): a blob is
// {"cmps", u32 method (2, 4 and 5 use the same decoder), u32 output size, stream}; the stream is an LZ with 32 bit big endian flag
// words (MSB first): 1 = literal byte, 0 = match {u16 v: length nibble = v >> 12 (0: the next byte + 0x12, else nibble + 2),
// distance = (v & 0xfff) + 1, bytes before the output start read as 0}. The decoded data is a table container {u32 count, 3 zero
// words, count x u32 offsets (0x20 aligned)}; event_new.bin is a sequence of 0x800 aligned blobs. Writes the decoded blob
// ("data.bin", or "NNNN/data.bin" for several blobs) and the table members ("NNNN.bin").
static bool cmps_decode (const u8 *d, size_t sz, u8 **out, size_t *outsz, size_t *used)
{
	if (sz < 16 || memcmp (d, "cmps", 4) || d[4] || d[5] || d[6] || (d[7] != 2 && d[7] != 4 && d[7] != 5))
		return false;
	const size_t n = rd_be32 (d + 8);
	if (n < 4 || n > 0x10000000)
		return false;
	u8 *o = MALLOC (n);
	if (!o)
		return false;
	size_t p = 12, w = 0;
	u32 fl = 0;
	uint bits = 0;
	while (w < n)
	{
		if (!bits)
		{
			if (p + 4 > sz)
				goto bad;
			fl = rd_be32 (d + p);
			p += 4;
			bits = 32;
		}
		if (fl & 0x80000000u)
		{
			if (p >= sz)
				goto bad;
			o[w++] = d[p++];
		}
		else
		{
			if (p + 2 > sz)
				goto bad;
			const uint v = (uint)d[p] << 8 | d[p + 1];
			p += 2;
			uint len = v >> 12;
			if (!len)
			{
				if (p >= sz)
					goto bad;
				len = d[p++] + 0x12u;
			}
			else
				len += 2;
			const size_t dist = (v & 0xfff) + 1;
			for (uint k = 0; k < len && w < n; k++, w++)
				o[w] = w >= dist ? o[w - dist] : 0;
		}
		fl <<= 1;
		bits--;
	}
	*out = o;
	*outsz = n;
	*used = p;
	return true;
bad:
	FREE (o);
	return false;
}

static enumError cmps_write_blob (ccp arg, ccp dest, ccp basedir, ccp sub, const u8 *o, size_t n)
{
	char name[64];
	enumError err = ERR_OK;
	snprintf (name, sizeof (name), "%sdata.bin", sub);
	err = shade_write (arg, dest, basedir, name, (u8 *)o, (uint)n);
	if (err || n < 0x30)
		return err;
	const u32 cnt = rd_be32 (o);
	if (!cnt || cnt > 100000 || 16 + 4 * (size_t)cnt > n || rd_be32 (o + 4) || rd_be32 (o + 8) || rd_be32 (o + 12))
		return err;
	for (u32 i = 0; i < cnt && !err; i++)
	{
		const size_t a = rd_be32 (o + 16 + 4 * (size_t)i);
		size_t b = i + 1 < cnt ? rd_be32 (o + 20 + 4 * (size_t)i) : n;
		if (a >= b || b > n)
			continue;
		snprintf (name, sizeof (name), "%s%04u.bin", sub, i);
		err = shade_write (arg, dest, basedir, name, (u8 *)o + a, (uint)(b - a));
	}
	return err;
}

static enumError extract_cmps_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 hd[8];
	const size_t got = fread (hd, 1, 8, f);
	fclose (f);
	if (got != 8 || memcmp (hd, "cmps\0\0\0", 7) || (hd[7] != 2 && hd[7] != 4 && hd[7] != 5))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 16 || sz > 0x20000000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	// blobs: at 0 and at every 0x800 boundary that starts a "cmps" header
	uint blobs = 0;
	for (size_t p = 0; p + 16 <= sz; p += 0x800)
		if (!memcmp (raw + p, "cmps\0\0\0", 7))
			blobs++;
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	enumError err = ERR_OK;
	uint done = 0;
	for (size_t p = 0; p + 16 <= sz && !err; p += 0x800)
	{
		if (memcmp (raw + p, "cmps\0\0\0", 7))
			continue;
		u8 *o = 0;
		size_t n = 0, used = 0;
		if (!cmps_decode (raw + p, sz - p, &o, &n, &used))
			continue;
		char sub[32] = "";
		if (blobs > 1)
			snprintf (sub, sizeof (sub), "%04u/", done);
		if (!testmode)
			err = cmps_write_blob (arg, dest, basedir, sub, o, n);
		FREE (o);
		done++;
	}
	FREE (raw);
	if (!done)
		return ERR_NOTHING_TO_DO;
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CMPS:%s (%u blobs) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, done, dest);
	return err;
}
