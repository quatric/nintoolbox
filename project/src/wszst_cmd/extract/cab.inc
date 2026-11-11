// Microsoft Cabinet ".cab" ("MSCF"): single-cabinet extraction of stored (type 0) and MSZIP (type 1: every CFDATA block is "CK" + raw deflate that uses the previous
// block's output as dictionary) folders; Quantum and LZX folders are reported and skipped. Files are written to "<file>.d/<path>" (backslashes become '/').
static u8 *cab_folder (const u8 *raw, size_t sz, size_t off, uint nblocks, uint type, uint cbdata, size_t *outn)
{
	size_t cap = 0, n = 0;
	u8 *out = 0;
	size_t p = off;
	for (uint b = 0; b < nblocks; b++)
	{
		if (p + 8 + cbdata > sz)
			break;
		const uint cb = raw[p + 4] | raw[p + 5] << 8, cu = raw[p + 6] | raw[p + 7] << 8;
		p += 8 + cbdata;
		if (p + cb > sz)
			break;
		if (n + cu > cap)
		{
			cap = (n + cu) * 2 + 65536;
			out = REALLOC (out, cap);
			if (!out)
				return 0;
		}
		if (type == 0)
		{
			if (cb != cu)
				break;
			memcpy (out + n, raw + p, cb);
		}
		else
		{
			if (cb < 2 || raw[p] != 'C' || raw[p + 1] != 'K')
				break;
			z_stream zs;
			memset (&zs, 0, sizeof (zs));
			if (inflateInit2 (&zs, -15) != Z_OK)
				break;
			if (n)
				inflateSetDictionary (&zs, out + (n > 32768 ? n - 32768 : 0), n > 32768 ? 32768 : (uInt)n);
			zs.next_in = (Bytef *)raw + p + 2;
			zs.avail_in = cb - 2;
			zs.next_out = out + n;
			zs.avail_out = cu;
			const int r = inflate (&zs, Z_SYNC_FLUSH);
			inflateEnd (&zs);
			if ((r != Z_OK && r != Z_STREAM_END) || zs.avail_out)
				break;
		}
		n += cu;
		p += cb;
	}
	*outn = n;
	return out;
}

static enumError extract_cab_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".cab") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 36 && !memcmp (raw, "MSCF", 4))
	{
		const size_t coff = raw[16] | raw[17] << 8 | raw[18] << 16 | (size_t)raw[19] << 24;
		const uint nfold = raw[26] | raw[27] << 8, nfile = raw[28] | raw[29] << 8, flags = raw[30] | raw[31] << 8;
		size_t p = 36;
		uint cbfold = 0, cbdata = 0;
		if (flags & 4)
		{
			cbfold = raw[p + 2];
			cbdata = raw[p + 3];
			p += 4 + (raw[p] | raw[p + 1] << 8);
		}
		for (uint k = 0; k < 4; k++) // names of the previous (flag 1) and next (flag 2) cabinet and their disks
			if (flags >> (k / 2) & 1)
			{
				while (p < sz && raw[p])
					p++;
				p++;
			}
		struct { size_t off; uint blocks, type; u8 *data; size_t n; } *fold = CALLOC (nfold ? nfold : 1, 32);
		bool ok = fold && nfold && nfile && p + (size_t)nfold * (8 + cbfold) <= sz && coff < sz;
		for (uint f = 0; ok && f < nfold; f++)
		{
			const u8 *h = raw + p + (size_t)f * (8 + cbfold);
			fold[f].off = h[0] | h[1] << 8 | h[2] << 16 | (size_t)h[3] << 24;
			fold[f].blocks = h[4] | h[5] << 8;
			fold[f].type = (h[6] | h[7] << 8) & 15;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		uint written = 0;
		if (ok)
		{
			err = ERR_OK;
			size_t q = coff;
			for (uint i = 0; i < nfile && q + 16 < sz && err == ERR_OK; i++)
			{
				const size_t fsz = raw[q] | raw[q + 1] << 8 | raw[q + 2] << 16 | (size_t)raw[q + 3] << 24;
				const size_t foff = raw[q + 4] | raw[q + 5] << 8 | raw[q + 6] << 16 | (size_t)raw[q + 7] << 24;
				const uint fi = raw[q + 8] | raw[q + 9] << 8;
				char name[PATH_MAX];
				size_t k = 0;
				for (q += 16; q < sz && raw[q] && k < sizeof (name) - 1; q++, k++)
					name[k] = raw[q] == '\\' ? '/' : raw[q];
				name[k] = 0;
				q++;
				if (fi >= nfold || !unixarch_safe (name) || fold[fi].type > 1)
				{
					if (fi < nfold && fold[fi].type > 1)
						fprintf (stdlog, "CAB: skipping %s (Quantum / LZX folder)\n", name);
					continue;
				}
				if (!fold[fi].data)
					fold[fi].data = cab_folder (raw, sz, fold[fi].off, fold[fi].blocks, fold[fi].type, cbdata, &fold[fi].n);
				if (!fold[fi].data || foff + fsz > fold[fi].n)
					continue;
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sEXTRACT CAB:%s -> %s/%s%s (%zu bytes)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, basedir ? basedir : "", name, fsz);
				err = shade_write (arg, dest, basedir, name, fold[fi].data + foff, (uint)fsz);
				written++;
			}
		}
		for (uint f = 0; fold && f < nfold; f++)
			FREE (fold[f].data);
		FREE (fold);
		if (!written && err == ERR_OK)
			err = ERR_NOTHING_TO_DO;
	}
	FREE (raw);
	return err;
}
