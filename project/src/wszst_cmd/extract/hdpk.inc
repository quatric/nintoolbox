// Nintendo Channel "HDPK001B" compiled table schemas (e.g. the xml/*.bin files of the Wii no Ma / Nintendo
// Channel family): "HDPK", "001B", u32be file size, u32be offset into the data area, a record table, then the
// NUL-separated table and column names, a u32 offset table into them and the "CHECKDATA" / "VERSION" markers.
// The record table is not decoded; <name>.d/strings.txt lists "offset<TAB>text" for the first string (the table
// name), the column names and the trailer markers. Matched by magic, any extension.
static enumError extract_hdpk_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x40 || memcmp (raw, "HDPK001", 7) || rd_be32 (raw + 8) != sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t cap = sz * 2 + 256, used = 0;
	char *out = MALLOC (cap);
	if (!out)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	used += snprintf (out, cap, "# HDPK%.4s, %zu bytes, header words %u %u %u\n", (ccp)raw + 4, sz,
		rd_be32 (raw + 12), rd_be32 (raw + 16), rd_be32 (raw + 20));
	uint n = 0;
	for (size_t o = 0x24; o < sz;)
	{
		size_t e = o;
		while (e < sz && raw[e] >= 0x20 && raw[e] < 0x7f)
			e++;
		if (e - o >= 3 && (e == sz || !raw[e]) && used + (e - o) + 24 < cap)
		{
			used += snprintf (out + used, cap - used, "%zu\t", o);
			memcpy (out + used, raw + o, e - o);
			used += e - o;
			out[used++] = '\n';
			n++;
		}
		o = e + 1;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT HDPK:%s (%u strings) -> %s/strings.txt\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n, dest);
	const enumError err = shade_write (arg, dest, basedir, "strings.txt", (u8 *)out, (uint)used);
	FREE (out);
	FREE (raw);
	return err;
}
