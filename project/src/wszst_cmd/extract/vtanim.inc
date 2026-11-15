// Virtua Tennis 2009 (Wii) animation parameter tables, headerless, big-endian: Players/<ID>/rundata.dat (56 byte
// records) and swingdata.dat (256 byte records). Every record starts with a 32 byte NUL padded animation name of the
// form "MYA_TP_F3_H1_SIM" / "ARI_STAND_LOOP_SIM" (three upper case letters and '_'), the last record is named "end", followed by unnamed words
// (a hash, ints and floats) whose meaning is not known. Recognised by the record pattern; written as "<file>.json".
static bool vtanim_check (const u8 *p, size_t sz, size_t rs)
{
	if (!sz || sz % rs)
		return false;
	for (size_t o = 0; o < sz; o += rs)
	{
		if (o + rs == sz && !memcmp (p + o, "end", 4))
			continue;
		size_t n = 0;
		while (n < 0x20 && p[o + n])
			n++;
		if (n < 5 || n >= 0x20 || p[o + 3] != '_')
			return false;
		for (size_t k = 0; k < 3; k++)
			if (p[o + k] < 'A' || p[o + k] > 'Z')
				return false;
		for (size_t k = 4; k < n; k++)
			if (!((p[o + k] >= 'A' && p[o + k] <= 'Z') || (p[o + k] >= 'a' && p[o + k] <= 'z') ||
			      (p[o + k] >= '0' && p[o + k] <= '9') || p[o + k] == '_'))
				return false;
		for (size_t k = n; k < 0x20; k++)
			if (p[o + k])
				return false;
	}
	return true;
}

static enumError extract_vtanim_file (ccp arg, ccp basedir, uint depth)
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
	size_t rs = 0;
	if (vtanim_check (raw, sz, 0x38))
		rs = 0x38;
	else if (vtanim_check (raw, sz, 0x100))
		rs = 0x100;
	if (!rs)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "{\n  \"record_size\": %u,\n  \"records\": [", (uint)rs);
	for (size_t o = 0; o < sz; o += rs)
	{
		pcr_emit (&c, "%s\n    {\"name\": \"%s\", \"words\": [", o ? "," : "", (const char *)raw + o);
		baao_words (&c, raw + o + 0x20, rs - 0x20);
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
			fprintf (stdlog, "WOULD EXTRACT VTANIM:%s -> JSON:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT VTANIM:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
#pragma pop_macro("free")
#pragma pop_macro("calloc")
