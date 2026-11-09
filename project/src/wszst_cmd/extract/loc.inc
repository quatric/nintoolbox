// Ubisoft Imagine "LOCV1.0" localisation table (Beastly, Babysitting Party, Babyz; the .LOC files are LZSS wrapped, see
// imaginelz.inc): ".LOCV1.0", u32 id, 4-byte language tag ("!DE\0"), then little-endian u32 string offsets starting at
// 0x10 up to the first string (the table end is the smallest 4-aligned offset that equals 0x10 + 4 * index; a few
// leading entries point at zero bytes inside the table and are empty strings), followed by NUL terminated Latin-1
// texts. strings.tsv has "index<TAB>text" (UTF-8, \n \r \t \\ escaped).
static enumError extract_loc_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x20 || memcmp (raw, ".LOCV1.0", 8))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t n = 0;
	for (size_t i = 0; 0x10 + 4 * (i + 1) <= sz; i++)
	{
		const u32 o = rd_le32 (raw + 0x10 + 4 * i);
		if (o > 0x10 && o <= sz && !((o - 0x10) & 3) && (o - 0x10) / 4 > i)
		{
			n = (o - 0x10) / 4;
			break;
		}
	}
	if (!n || 0x10 + 4 * n > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t cap = sz * 3 + 32 * n + 64, used = 0;
	char *out = MALLOC (cap);
	if (!out)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	for (size_t i = 0; i < n; i++)
	{
		const u32 o = rd_le32 (raw + 0x10 + 4 * i);
		used += snprintf (out + used, cap - used, "%zu\t", i);
		if (o >= 0x10 && o < sz)
			for (const u8 *p = raw + o; p < raw + sz && *p; p++)
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
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT LOC:%s (%zu strings) -> %s/strings.tsv\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n, dest);
	const enumError err = shade_write (arg, dest, basedir, "strings.tsv", (u8 *)out, (uint)used);
	FREE (out);
	FREE (raw);
	return err;
}
