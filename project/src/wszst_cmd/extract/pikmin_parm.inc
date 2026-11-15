// Pikmin 1 tagged parameter lists (parms/*.bin, camera and similar; Parameters::read in the decompilation): repeated
// {3 character id, u8 size} + `size` bytes, ended by 0xffffffff. A file is one such list, or several back to back (naviMgr, pikiMgr, pelMgr), optionally after a 4 or 16 byte prefix (a list count, or the `cric` + 3 floats of tstgen) which is written as hex. The decompilation names
// the parameters per class and the files carry no types, so values are written by size: 1/2 byte integers, 4 bytes as
// a float when it looks like one (otherwise an int), printable data as text, 12 bytes as three floats, anything else
// as hex. Written as "<name>.bin.json" ({"p00": 7.0, ...}); duplicated ids become arrays.
static bool parm_float_like (u32 w)
{
	if (!w || w == 0x80000000u)
		return false;
	const uint e = (w >> 23) & 0xff;
	if (e < 0x60 || e > 0x9a) // |v| between ~1e-10 and ~1e12
		return false;
	return w > 0xffff && (s32)w >= 0 ? true : (s32)w < 0 && w < 0xffffff00u;
}

// JSON string body: quotes, backslashes, controls and non-ASCII bytes (Shift-JIS text) as \u00XX escapes
static void parm_emit_str (pcr_ctx_t *c, const u8 *t, uint len)
{
	pcr_emit (c, "\"");
	for (uint i = 0; i < len; i++)
		if (t[i] < 0x20 || t[i] >= 0x7f || t[i] == '"' || t[i] == '\\')
			pcr_emit (c, "\\u%04x", t[i]);
		else
			pcr_emit (c, "%c", t[i]);
	pcr_emit (c, "\"");
}

// Validates one list at p; returns the offset behind its terminator or 0.
static size_t parm_list_end (const u8 *raw, size_t sz, size_t p)
{
	uint n = 0;
	while (p + 4 <= sz)
	{
		const u32 h = rd_be32 (raw + p);
		if (h == 0xffffffffu)
			return n ? p + 4 : 0;
		for (int k = 0; k < 3; k++)
		{
			const u8 c = raw[p + k];
			if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
				return 0;
		}
		if ((h & 0xff) == 8)
		{
			// length-prefixed string parameter (the size byte does not count the text)
			if (p + 8 > sz || rd_be32 (raw + p + 4) > 256)
				return 0;
			p += 8 + rd_be32 (raw + p + 4);
		}
		else
			p += 4 + (h & 0xff);
		n++;
	}
	return 0;
}

// pelMgr style record: list, three 4 character ids, one more word (-1, or a small number)
static size_t parm_record_end (const u8 *raw, size_t sz, size_t p)
{
	const size_t e = parm_list_end (raw, sz, p);
	if (!e || e + 16 > sz)
		return 0;
	for (int k = 0; k < 12; k++)
		if (raw[e + k] < 0x20 || raw[e + k] > 0x7e)
			return 0;
	return e + 16;
}

// Writes the list at p as a JSON object; returns the offset behind it.
static size_t parm_emit_list (pcr_ctx_t *c, const u8 *raw, size_t p)
{
	pcr_emit (c, "{");
	for (uint i = 0;; i++)
	{
		const u32 h = rd_be32 (raw + p);
		if (h == 0xffffffffu)
		{
			p += 4;
			break;
		}
		const uint vs = h & 0xff;
		const u8 *v = raw + p + 4;
		pcr_emit (c, "%s\n  \"%c%c%c\": ", i ? "," : "", raw[p], raw[p + 1], raw[p + 2]);
		if (vs == 8)
		{
			const uint l = rd_be32 (v);
			uint tl = 0;
			while (tl < l && v[4 + tl])
				tl++;
			parm_emit_str (c, v + 4, tl);
			p += 8 + l;
			continue;
		}
		bool text = vs > 1;
		for (uint k = 0; k < vs && text; k++)
			if (!(v[k] == 0 || (v[k] >= 0x20 && v[k] < 0x7f)))
				text = false;
		if (vs == 4 && !(text && v[0] && v[1] && v[2] && v[3]))
			text = false;
		if (vs == 4)
		{
			const u32 w = rd_be32 (v);
			float f;
			memcpy (&f, &w, 4);
			if (parm_float_like (w))
				pcr_emit (c, "%.9g", (double)f);
			else
				pcr_emit (c, "%d", (int)(s32)w);
		}
		else if (vs == 1)
			pcr_emit (c, "%u", v[0]);
		else if (vs == 2)
			pcr_emit (c, "%u", rd_be16 (v));
		else if (vs == 12 && !text)
		{
			float f[3];
			for (int k = 0; k < 3; k++)
			{
				const u32 w = rd_be32 (v + 4 * k);
				memcpy (f + k, &w, 4);
			}
			pcr_emit (c, "[%.9g, %.9g, %.9g]", (double)f[0], (double)f[1], (double)f[2]);
		}
		else if (text)
		{
			uint l = 0;
			while (l < vs && v[l])
				l++;
			parm_emit_str (c, v, l);
		}
		else
		{
			pcr_emit (c, "\"");
			for (uint k = 0; k < vs; k++)
				pcr_emit (c, "%02x", v[k]);
			pcr_emit (c, "\"");
		}
		p += 4 + vs;
	}
	pcr_emit (c, "\n}");
	return p;
}

static enumError extract_pikmin_parm_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	static const size_t prefixes[4] = { 0, 4, 16, 4 };
	size_t prefix = 0, nlists = 0;
	bool records = false;
	for (int k = 0; k < 4 && !nlists; k++)
	{
		size_t p = prefixes[k], cnt = 0;
		while (p < sz)
		{
			const size_t e = k == 3 ? parm_record_end (raw, sz, p) : parm_list_end (raw, sz, p);
			if (!e)
				break;
			p = e;
			cnt++;
		}
		if (cnt && p == sz && (prefixes[k] != 4 || rd_be32 (raw) == cnt))
		{
			prefix = prefixes[k];
			nlists = cnt;
			records = k == 3;
		}
	}
	if (!nlists)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	size_t p = prefix;
	if (prefix || nlists > 1)
	{
		pcr_emit (&c, "{\n\"prefix\": \"");
		for (size_t k = 0; k < prefix; k++)
			pcr_emit (&c, "%02x", raw[k]);
		pcr_emit (&c, "\",\n\"lists\": [");
		for (size_t i = 0; i < nlists; i++)
		{
			pcr_emit (&c, "%s\n", i ? "," : "");
			if (records)
			{
				pcr_emit (&c, "{\"params\": ");
				p = parm_emit_list (&c, raw, p);
				pcr_emit (&c, ", \"ids\": [");
				for (int k = 0; k < 3; k++)
				{
					if (k)
						pcr_emit (&c, ", ");
					parm_emit_str (&c, raw + p + 4 * k, 4);
				}
				pcr_emit (&c, "], \"tail\": %d}", (int)(s32)rd_be32 (raw + p + 12));
				p += 16;
			}
			else
				p = parm_emit_list (&c, raw, p);
		}
		pcr_emit (&c, "\n]\n}\n");
	}
	else
	{
		parm_emit_list (&c, raw, p);
		pcr_emit (&c, "\n");
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.json", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT PARMS:%s -> JSON:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT PARMS:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
