// WSC Real 09 / World Snooker Championship broken-shot scenarios (Wii: shots/brokenshots/data/*.dat), little-endian,
// exactly 2070 bytes: char name[0x20] ("2downandcueball", ...), three u16 words, then 127 records of 16 bytes
// (four LE words each; the first 16 records are ball positions {x, y, 1.125 radius, 0}). The remaining records and
// the three header words are not understood. Written as "<file>.json" with the words unnamed (floats where the
// value looks like one).
static enumError extract_wscshot_file (ccp arg, ccp basedir, uint depth)
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
	bool good = sz == 2070;
	size_t nl = 0;
	while (good && nl < 0x20 && raw[nl])
	{
		if (raw[nl] < 0x20 || raw[nl] > 0x7e || raw[nl] == '"')
			good = false;
		nl++;
	}
	for (size_t i = nl; good && i < 0x20; i++)
		if (raw[i])
			good = false;
	good = good && nl >= 3 && nl < 0x20;
	if (!good)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "{\n  \"name\": ");
	parm_emit_str (&c, raw, (uint)nl);
	pcr_emit (&c, ",\n  \"header\": [%u, %u, %u],\n  \"records\": [",
		raw[0x20] | (raw[0x21] << 8), raw[0x22] | (raw[0x23] << 8), raw[0x24] | (raw[0x25] << 8));
	for (uint r = 0; r < 127; r++)
	{
		const u8 *p = raw + 0x26 + 16 * (size_t)r;
		pcr_emit (&c, "%s\n    [", r ? "," : "");
		for (uint k = 0; k < 4; k++)
		{
			const u32 w = (u32)p[4 * k] | ((u32)p[4 * k + 1] << 8) | ((u32)p[4 * k + 2] << 16) | ((u32)p[4 * k + 3] << 24);
			float f;
			memcpy (&f, &w, 4);
			if (gff_float_like (w))
				pcr_emit (&c, "%s%.9g", k ? ", " : "", (double)f);
			else
				pcr_emit (&c, "%s%u", k ? ", " : "", w);
		}
		pcr_emit (&c, "]");
	}
	pcr_emit (&c, "\n  ]\n}\n");
	enumError err = ERR_NOTHING_TO_DO;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.json", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT WSCSHOT:%s -> JSON:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT WSCSHOT:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
#pragma pop_macro("free")
#pragma pop_macro("calloc")
