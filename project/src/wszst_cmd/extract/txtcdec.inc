// 10 Minute Solution / Zhu Zhu Pets (Wii) "*.txtc" compressed text catalogs: a flag-byte LZSS stream (flags read LSB first, 1 = literal byte, 0 = 2-byte match {b0, b1}: ring buffer position (b0 | b1 << 8) >> 4 & 0xfff,
// length (b0 & 15) + 3, 4096-byte ring starting empty at position 0) whose output is the "BA DB EE F1" catalog: {magic, u32 0, u16 language count, u16 ids, u16 keys, u16 extra keys, {u16 length, UTF-16BE key}, then per language {u32 bytes, NUL-separated UTF-16BE strings}}. The inflated catalog is written to
// "<file>.dec" and the key and per-language strings (UTF-8; \n, \t, \\ escaped) to "<file>.tsv".
// emits "len" UTF-16BE code units (escaped) as UTF-8
static void txtc_emit16 (pcr_ctx_t *c, const u8 *s, uint len)
{
	for (uint k = 0; k < len; k++)
	{
		uint u = rd_be16 (s + 2 * k);
		if (u >= 0xd800 && u < 0xdc00 && k + 1 < len)
			u = 0x10000 + ((u - 0xd800) << 10) + (rd_be16 (s + 2 * k + 2) - 0xdc00), k++;
		if (u == '\n')
			pcr_emit (c, "\\n");
		else if (u == '\t')
			pcr_emit (c, "\\t");
		else if (u == '\\')
			pcr_emit (c, "\\\\");
		else if (u == '\r')
			;
		else if (u < 0x80)
			pcr_emit (c, "%c", (int)u);
		else if (u < 0x800)
			pcr_emit (c, "%c%c", (int)(0xc0 | u >> 6), (int)(0x80 | (u & 63)));
		else if (u < 0x10000)
			pcr_emit (c, "%c%c%c", (int)(0xe0 | u >> 12), (int)(0x80 | (u >> 6 & 63)), (int)(0x80 | (u & 63)));
		else
			pcr_emit (c, "%c%c%c%c", (int)(0xf0 | u >> 18), (int)(0x80 | (u >> 12 & 63)), (int)(0x80 | (u >> 6 & 63)), (int)(0x80 | (u & 63)));
	}
}

static enumError extract_txtcdec_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".txtc"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *out = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	if (sz < 16 || raw[1] != 0xba || raw[2] != 0xdb || raw[3] != 0xee)
	{
		// flag byte first: the magic itself is stored as literals
		if (!(sz > 16 && raw[1] == 0xba && raw[2] == 0xdb))
			goto done;
	}
	{
		const size_t max = 64u << 20;
		out = MALLOC (max);
		if (!out)
			goto done;
		u8 *ring = CALLOC (1, 4096);
		if (!ring)
			goto done;
		size_t p = 0, o = 0;
		uint r = 0;
		while (p < sz && o + 20 < max)
		{
			const uint fl = raw[p++];
			for (uint b = 0; b < 8 && p < sz && o + 20 < max; b++)
			{
				if (fl >> b & 1)
				{
					const u8 ch = raw[p++];
					out[o++] = ch;
					ring[r] = ch;
					r = (r + 1) & 4095;
				}
				else
				{
					if (p + 2 > sz)
						break;
					const uint v = raw[p] | raw[p + 1] << 8, off = v >> 4 & 0xfff, ln = (raw[p] & 15) + 3;
					p += 2;
					for (uint i = 0; i < ln; i++)
					{
						const u8 ch = ring[(off + i) & 4095];
						out[o++] = ch;
						ring[r] = ch;
						r = (r + 1) & 4095;
					}
				}
			}
		}
		FREE (ring);
		if (o < 16 || memcmp (out, "\xba\xdb\xee\xf1", 4))
			goto done;
		char dec[PATH_MAX], tsv[PATH_MAX];
		snprintf (dec, sizeof (dec), "%s.dec", arg);
		snprintf (tsv, sizeof (tsv), "%s.tsv", arg);
		c.d = out;
		c.sz = o;
		c.ok = true;
		uint n = 0;
		{
			// {magic, u32 0, u16 languages, u16 ids[], u16 keys, u16 extra keys, {u16 len, UTF-16BE key}[], then per language {u32 bytes, NUL-separated UTF-16BE strings}}
			size_t q = 0xc;
			const uint nl = q + 2 <= o ? rd_be16 (out + q) : 0;
			if (!nl || nl > 16 || q + 2 + 2 * (size_t)nl + 4 > o)
			{
				err = ERR_NOTHING_TO_DO;
				goto done;
			}
			uint ids[16];
			for (uint l = 0; l < nl; l++)
				ids[l] = rd_be16 (out + q + 2 + 2 * l);
			q += 2 + 2 * (size_t)nl;
			const uint nk = rd_be16 (out + q) + rd_be16 (out + q + 2);
			q += 4;
			size_t kpos[1024];
			uint klen[1024];
			if (!nk || nk > 1024)
				goto done;
			for (uint i = 0; i < nk; i++)
			{
				if (q + 2 > o)
					goto done;
				klen[i] = rd_be16 (out + q);
				kpos[i] = q + 2;
				q += 2 + 2 * (size_t)klen[i];
				if (q > o)
					goto done;
			}
			size_t vpos[16], vend[16];
			for (uint l = 0; l < nl; l++)
			{
				if (q + 4 > o)
					goto done;
				const u32 vs = rd_be32 (out + q);
				q += 4;
				if (q + vs > o)
					goto done;
				vpos[l] = q;
				vend[l] = q + vs;
				q += vs;
			}
			pcr_emit (&c, "key");
			for (uint l = 0; l < nl; l++)
				pcr_emit (&c, "\tlang%u", ids[l]);
			pcr_emit (&c, "\n");
			for (uint l = 0; l < nl; l++)
				;
			size_t cur[16];
			for (uint l = 0; l < nl; l++)
				cur[l] = vpos[l];
			for (uint i = 0; i < nk; i++)
			{
				txtc_emit16 (&c, out + kpos[i], klen[i]);
				for (uint l = 0; l < nl; l++)
				{
					pcr_emit (&c, "\t");
					size_t e = cur[l];
					while (e + 2 <= vend[l] && (out[e] || out[e + 1]))
						e += 2;
					txtc_emit16 (&c, out + cur[l], (uint)((e - cur[l]) / 2));
					cur[l] = e + 2;
				}
				pcr_emit (&c, "\n");
				n++;
			}
		}
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT TXTC:%s (%u strings) -> %s\n", arg, n, tsv);
			err = ERR_OK;
		}
		else
		{
			File_t F;
			err = CreateFileOpt (&F, true, dec, false, arg);
			if (F.f && fwrite (out, 1, o, F.f) != o)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)o, dec);
			ResetFile (&F, opt_preserve);
			if (!err)
			{
				err = CreateFileOpt (&F, true, tsv, false, arg);
				if (F.f && fwrite (c.out, 1, c.len, F.f) != c.len)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)c.len, tsv);
				ResetFile (&F, opt_preserve);
			}
			if (verbose >= 0 && !err)
				fprintf (stdlog, "%sEXTRACT TXTC:%s (%u strings) -> %s\n", verbose > 0 ? "\n" : "", arg, n, tsv);
		}
	}
done:
	FREE (c.out);
	FREE (out);
	FREE (raw);
	return err;
}
