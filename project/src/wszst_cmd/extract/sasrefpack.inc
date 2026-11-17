// EA Sports (Tiger Woods PGA Tour, Wii) "data/stmfiles/*.sas" swing-capture streams: a chain of independent RefPack ("10 fb" + 24-bit size) chunks, each starting on a 0x800 boundary
// (next chunk = end of the stream's terminator opcode rounded up to 0x800). The chunks are decompressed and concatenated to "<file>.dec"; their record layout is not decoded.
static size_t sas_refpack_chunk (const u8 *s, size_t n, u8 *out, size_t out_cap, size_t *out_size)
{
	if (n < 6 || s[0] != 0x10 || s[1] != 0xfb)
		return 0;
	size_t p = 2;
	const size_t size = ((size_t)s[p] << 16) | ((size_t)s[p + 1] << 8) | s[p + 2];
	p += 3;
	if (!size || size > out_cap)
		return 0;
	size_t q = 0;
	while (p < n)
	{
		const uint a = s[p++];
		size_t lit = 0, len = 0, dist = 0;
		if (a < 0x80)
		{
			if (p + 1 > n)
				return 0;
			lit = a & 3;
			len = ((a & 0x1c) >> 2) + 3;
			dist = ((a & 0x60) << 3) + s[p++] + 1;
		}
		else if (a < 0xc0)
		{
			if (p + 2 > n)
				return 0;
			lit = s[p] >> 6;
			len = (a & 0x3f) + 4;
			dist = ((s[p] & 0x3f) << 8) + s[p + 1] + 1;
			p += 2;
		}
		else if (a < 0xe0)
		{
			if (p + 3 > n)
				return 0;
			lit = a & 3;
			len = ((a & 0x0c) << 6) + s[p + 2] + 5;
			dist = ((a & 0x10) << 12) + (s[p] << 8) + s[p + 1] + 1;
			p += 3;
		}
		else if (a < 0xfc)
			lit = ((a & 0x1f) << 2) + 4;
		else
			lit = a & 3;
		if (lit > n - p || lit > size - q)
			return 0;
		memcpy (out + q, s + p, lit);
		p += lit;
		q += lit;
		if (a >= 0xe0)
		{
			if (a >= 0xfc)
				break;
			continue;
		}
		if (dist > q || len > size - q)
			return 0;
		for (size_t i = 0; i < len; i++, q++)
			out[q] = out[q - dist];
	}
	if (q != size)
		return 0;
	*out_size = q;
	return p;
}

static enumError extract_sasrefpack_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".sas"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *buf = MALLOC (0x1000000);
	size_t o = 0, nchunks = 0;
	if (sz < 0x800 || raw[0] != 0x10 || raw[1] != 0xfb)
		goto done;
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.dec", arg);
		File_t F;
		F.f = 0;
		bool open = false;
		err = ERR_OK;
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SAS:%s -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
		while (o < sz && !testmode)
		{
			size_t dsz = 0;
			const size_t used = sas_refpack_chunk (raw + o, sz - o, buf, 0x1000000, &dsz);
			if (!used)
				break;
			if (!open)
			{
				err = CreateFileOpt (&F, true, dest, false, arg);
				open = true;
			}
			if (!err && F.f && fwrite (buf, 1, dsz, F.f) != dsz)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)dsz, dest);
			if (err)
				break;
			nchunks++;
			o = (o + used + 0x7ff) & ~(size_t)0x7ff;
		}
		if (open)
			ResetFile (&F, opt_preserve);
		if (!nchunks && !testmode)
			err = ERR_NOTHING_TO_DO;
	}
done:
	FREE (buf);
	FREE (raw);
	return err;
}
