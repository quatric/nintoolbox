// Sid Meier's Pirates! (Wii) "NANDBigFile.gda": a back-to-back sequence of Gamebryo streams. Each entry is either a raw "Gamebryo File Format, Version 20.6.0.0" NIF (length derived from its block table) or a compressed record
// {be32 parts (1..64), parts * {be32 compressed size, LZ11 stream (<= 32 KiB per part)}} whose concatenated output is a NIF or a "Gamebryo KFM" animation file. Raw KFM blobs run to the next entry.
// Entries are written to "<file>.d/NNNN/model.nif" (one directory per NIF so the extracted texture PNGs / GLB do not collide) or "NNNN.kfm" and recursed.
static const char gda_magic[] = "Gamebryo File Format, Version 20.6.0.0\n";

static u32 gda_rd32 (const u8 *p, bool le)
{
	return le ? rd_le32 (p) : rd_be32 (p);
}

static size_t gda_nif_end (const u8 *b, size_t sz)
{
	const size_t m = sizeof (gda_magic) - 1;
	if (sz < m + 20)
		return 0;
	const bool le = b[m + 4] == 1;
	const uint nb = rd_le32 (b + m + 9);
	size_t q = m + 13;
	if (!nb || nb > 100000 || q + 2 > sz)
		return 0;
	const uint nt = le ? rd_le16 (b + q) : rd_be16 (b + q);
	q += 2;
	for (uint i = 0; i < nt; i++)
	{
		if (q + 4 > sz)
			return 0;
		q += 4 + (size_t)gda_rd32 (b + q, le);
	}
	q += 2 * (size_t)nb;
	if (q + 4 * (size_t)nb + 8 > sz)
		return 0;
	u64 tot = 0;
	for (uint i = 0; i < nb; i++)
		tot += gda_rd32 (b + q + 4 * (size_t)i, le);
	q += 4 * (size_t)nb;
	const uint ns = gda_rd32 (b + q, le);
	q += 8;
	for (uint i = 0; i < ns; i++)
	{
		if (q + 4 > sz)
			return 0;
		q += 4 + (size_t)gda_rd32 (b + q, le);
	}
	if (q + 4 > sz)
		return 0;
	q += 4 + 4 * (size_t)gda_rd32 (b + q, le);
	if (q + tot + 4 > sz)
		return 0;
	q += (size_t)tot;
	const uint nr = gda_rd32 (b + q, le);
	if (nr > 1024)
		return 0;
	q += 4 + 4 * (size_t)nr;
	return q <= sz ? q : 0;
}

// decodes a compressed record at p; returns the next position or 0 when not a record
static size_t gda_record (const u8 *d, size_t sz, size_t p, u8 **out, uint *out_len)
{
	if (p + 12 > sz)
		return 0;
	const uint parts = rd_be32 (d + p);
	if (!parts || parts > 64)
		return 0;
	u8 *buf = 0;
	uint len = 0;
	size_t q = p + 4;
	for (uint i = 0; i < parts; i++)
	{
		if (q + 8 > sz || d[q + 4] != 0x11)
		{
			FREE (buf);
			return 0;
		}
		const size_t cs = rd_be32 (d + q);
		if (cs < 5 || q + 4 + cs > sz)
		{
			FREE (buf);
			return 0;
		}
		u8 *dec = 0;
		uint dl = 0;
		if (DecodeLZ10LZ11 (&dec, &dl, d + q + 4, (uint)cs) || !dec)
		{
			FREE (dec);
			FREE (buf);
			return 0;
		}
		u8 *nb = MALLOC ((size_t)len + dl);
		if (len)
			memcpy (nb, buf, len);
		memcpy (nb + len, dec, dl);
		FREE (buf);
		FREE (dec);
		buf = nb;
		len += dl;
		q += 4 + cs;
	}
	*out = buf;
	*out_len = len;
	return q;
}

static bool gda_is_start (const u8 *d, size_t sz, size_t q)
{
	if (q + sizeof (gda_magic) <= sz && !memcmp (d + q, gda_magic, sizeof (gda_magic) - 1))
		return true;
	if (q + 12 < sz && !d[q] && !d[q + 1] && !d[q + 2] && d[q + 3] >= 1 && d[q + 3] <= 64 && d[q + 8] == 0x11)
	{
		u8 *o = 0;
		uint l = 0;
		const size_t nx = gda_record (d, sz, q, &o, &l);
		const bool ok = nx && l > 20 && (!memcmp (o, ";Gamebryo KFM File V", 20) || !memcmp (o, gda_magic, 20));
		FREE (o);
		return ok;
	}
	return false;
}

static enumError gda_write (ccp arg, ccp dest, uint idx, const u8 *b, size_t len, ccp name)
{
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/%s", dest, name);
	(void)idx;
	File_t F;
	enumError err = CreateFileOpt (&F, true, path, false, arg);
	if (F.f && len && fwrite (b, 1, len, F.f) != len)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", len, path);
	ResetFile (&F, opt_preserve);
	return err;
}

static enumError extract_gda_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".gda") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x100 || memcmp (raw, gda_magic, sizeof (gda_magic) - 1))
		goto done;
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT GDA:%s -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
		err = ERR_OK;
		if (testmode)
			goto done;
		size_t p = 0;
		uint idx = 0;
		while (p + 16 < sz && !err)
		{
			u8 *buf = 0;
			uint len = 0;
			const u8 *data = 0;
			size_t dlen = 0, next = 0;
			if (!memcmp (raw + p, gda_magic, sizeof (gda_magic) - 1))
			{
				dlen = gda_nif_end (raw + p, sz - p);
				if (!dlen)
					break;
				data = raw + p;
				next = p + dlen;
			}
			else
			{
				next = gda_record (raw, sz, p, &buf, &len);
				if (next)
				{
					data = buf;
					dlen = len;
				}
				else
				{
					size_t q = p + 1;
					while (q + 16 < sz && !gda_is_start (raw, sz, q))
						q++;
					if (q + 16 >= sz)
						q = sz;
					data = raw + p;
					dlen = q - p;
					next = q;
				}
			}
			char name[64];
			const bool nif = dlen > 39 && !memcmp (data, gda_magic, 20);
			if (nif)
			{
				snprintf (name, sizeof (name), "%04u/model.nif", idx);
				const size_t e = gda_nif_end (data, dlen);
				if (e && e < dlen)
					dlen = e;
			}
			else
				snprintf (name, sizeof (name), "%04u.%s", idx, dlen > 20 && !memcmp (data, ";Gamebryo KFM File V", 20) ? "kfm" : "bin");
			err = gda_write (arg, dest, idx, data, dlen, name);
			FREE (buf);
			idx++;
			p = next;
		}
		if (!err)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (raw);
	return err;
}
