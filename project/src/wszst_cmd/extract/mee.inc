// Super Fruit Fall (Wii) ".mee" localisation tables (lang*.mee, *strings.mee), little-endian: either
// {u32 count, u32 key bytes, u32 text bytes} (12 byte head) or {u32 0, u32 count, u32 key bytes, u32 0, u32 text bytes, u32 0}
// (24 byte head), followed by "count" NUL terminated string identifiers and then "count" NUL terminated texts
// (Latin-1). The "MEEX" code image (code.mee) is not decoded. strings.tsv has "id<TAB>text" with \n \t \\ escaped.
static enumError extract_mee_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".mee"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	uint count = 0, ksz = 0, tsz = 0;
	size_t head = 0;
	if (sz >= 24 && rd_le32 (raw) == 0 && rd_le32 (raw + 12) == 0 && 24 + (size_t)rd_le32 (raw + 8) + rd_le32 (raw + 16) == sz)
	{
		head = 24;
		count = rd_le32 (raw + 4);
		ksz = rd_le32 (raw + 8);
		tsz = rd_le32 (raw + 16);
	}
	else if (sz >= 12 && 12 + (size_t)rd_le32 (raw + 4) + rd_le32 (raw + 8) == sz)
	{
		head = 12;
		count = rd_le32 (raw);
		ksz = rd_le32 (raw + 4);
		tsz = rd_le32 (raw + 8);
	}
	if (!head || !count || count > 100000 || !ksz || !tsz || raw[head + ksz - 1] || raw[sz - 1])
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t cap = (size_t)sz * 3 + 64 * (size_t)count + 64, used = 0;
	char *out = MALLOC (cap);
	if (!out)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	const u8 *k = raw + head, *kend = k + ksz, *t = kend, *tend = raw + sz;
	uint n = 0;
	for (; n < count && k < kend; n++)
	{
		const u8 *ke = memchr (k, 0, kend - k);
		if (!ke || t >= tend)
			break;
		const u8 *te = memchr (t, 0, tend - t);
		if (!te)
			break;
		used += snprintf (out + used, cap - used, "%s\t", (ccp)k);
		for (const u8 *p = t; p < te; p++)
		{
			if (*p == '\n')
				used += snprintf (out + used, cap - used, "\\n");
			else if (*p == '\r')
				used += snprintf (out + used, cap - used, "\\r");
			else if (*p == '\t')
				used += snprintf (out + used, cap - used, "\\t");
			else if (*p == '\\')
				used += snprintf (out + used, cap - used, "\\\\");
			else if (*p < 0x80)
				out[used++] = (char)*p;
			else
			{
				out[used++] = (char)(0xc0 | (*p >> 6));
				out[used++] = (char)(0x80 | (*p & 0x3f));
			}
		}
		out[used++] = '\n';
		k = ke + 1;
		t = te + 1;
	}
	if (n != count)
	{
		FREE (out);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT MEE:%s (%u strings) -> %s/strings.tsv\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);
	const enumError err = shade_write (arg, dest, basedir, "strings.tsv", (u8 *)out, (uint)used);
	FREE (out);
	FREE (raw);
	return err;
}
