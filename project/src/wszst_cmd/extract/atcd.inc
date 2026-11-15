// "ATCD" text catalog (Safar'Wii, Wii: 2d/<lang>/text/text_*.dat), big-endian:
//   {"ATCD", u32 0, u32 column hint, u32 entry stride, u32 entry count, u32 entries offset (0x20), u32, u32}, then
//   count entries of `stride` bytes: a 0x20 byte ASCII key followed by u32 words; words that point behind the entry
//   table to a NUL terminated UTF-16BE string (even offsets, shared strings allowed) are written as strings, the
//   others as numbers. The string area follows the entry table. Written as "<file>.json".
static bool atcd_str_ok (const u8 *raw, size_t sz, size_t o, size_t *len)
{
	if (o & 1)
		return false;
	size_t n = 0;
	while (o + 2 * n + 2 <= sz && (raw[o + 2 * n] || raw[o + 2 * n + 1]))
		n++;
	if (o + 2 * n + 2 > sz)
		return false;
	*len = n;
	return true;
}

static enumError extract_atcd_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".dat"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t stride = sz >= 0x20 ? rd_be32 (raw + 12) : 0, cnt = sz >= 0x20 ? rd_be32 (raw + 16) : 0,
		     eo = sz >= 0x20 ? rd_be32 (raw + 20) : 0;
	const size_t tab = eo + cnt * stride;
	if (sz < 0x20 || memcmp (raw, "ATCD", 4) || stride < 0x24 || stride > 0x100 || (stride & 3) || !cnt || cnt > 100000
	    || eo != 0x20 || tab > sz || rd_be32 (raw + 4) != 0)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "{\n  \"entries\": [");
	for (size_t i = 0; i < cnt; i++)
	{
		const u8 *e = raw + eo + i * stride;
		pcr_emit (&c, "%s\n    {\"key\": ", i ? "," : "");
		parm_emit_str (&c, e, (uint)strnlen ((const char *)e, 0x20));
		pcr_emit (&c, ", \"values\": [");
		for (size_t k = 0x20; k + 4 <= stride; k += 4)
		{
			const u32 w = rd_be32 (e + k);
			size_t len;
			pcr_emit (&c, "%s", k > 0x20 ? ", " : "");
			if (w >= tab && w < sz && atcd_str_ok (raw, sz, w, &len))
			{
				pcr_emit (&c, "\"");
				for (size_t q = 0; q < len; q++)
				{
					const uint u = rd_be16 (raw + w + 2 * q);
					if (u < 0x20 || u >= 0x7f || u == '"' || u == '\\')
						pcr_emit (&c, "\\u%04x", u);
					else
						pcr_emit (&c, "%c", (char)u);
				}
				pcr_emit (&c, "\"");
			}
			else
				pcr_emit (&c, "%u", w);
		}
		pcr_emit (&c, "]}");
	}
	pcr_emit (&c, "\n  ]\n}\n");
	enumError err = ERR_NOTHING_TO_DO;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.json", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT ATCD:%s -> JSON:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT ATCD:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
#pragma pop_macro("free")
#pragma pop_macro("calloc")
