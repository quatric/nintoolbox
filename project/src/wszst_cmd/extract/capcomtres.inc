// Capcom "TRES" text resources (Resident Evil Umbrella / Darkside Chronicles ".txtres"). Big-endian: "TRES",
// u32 version, u32 record count @8, then count u32 offsets from 0x10 pointing at 0x14-byte records
// {u32 id, u32 label string offset (0 = none), u32 string count, u32 first text offset, u32 0}. The text
// strings are NUL-terminated UTF-8, consecutive for records with several. Written to "<file>.d/text.txt" as
// "id<TAB>label<TAB>text<TAB>text..." with newlines / tabs / backslashes escaped.
static enumError extract_capcomtres_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (export_count <= 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint n = sz >= 0x14 && !memcmp (raw, "TRES", 4) ? rd_be32 (raw + 8) : 0;
	if (!n || n > 0x100000 || (size_t)0x10 + 4 * (size_t)n > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t cap = sz * 2 + 64, len = 0;
	char *out = MALLOC (cap);
	if (!out)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
#define TRES_PUT(c) (out[len++] = (c))
	for (uint i = 0; i < n; i++)
	{
		const uint ro = rd_be32 (raw + 0x10 + 4 * i);
		if (ro > sz || sz - ro < 0x14)
			continue;
		const uint id = rd_be32 (raw + ro), lab = rd_be32 (raw + ro + 4), cnt = rd_be32 (raw + ro + 8),
			   txt = rd_be32 (raw + ro + 12);
		if (txt >= sz || cnt > 16 || len + 64 > cap)
			continue;
		len += snprintf (out + len, cap - len, "%08x", id);
		size_t o = lab;
		for (uint k = 0; k <= cnt; k++)
		{
			if (k == 0 && (!lab || lab >= sz))
			{
				TRES_PUT ('\t');
				continue;
			}
			if (k == 1)
				o = txt;
			else if (k && o >= sz)
				break;
			if (len + 2 * (sz - o) + 8 > cap)
			{
				cap = cap * 2 + 2 * (sz - o) + 8;
				char *nb = REALLOC (out, cap);
				if (!nb)
				{
					FREE (out);
					FREE (raw);
					return ERR_OUT_OF_MEMORY;
				}
				out = nb;
			}
			TRES_PUT ('\t');
			for (; o < sz && raw[o]; o++)
			{
				const char c = (char)raw[o];
				if (c == '\n' || c == '\t' || c == '\\' || c == '\r')
				{
					TRES_PUT ('\\');
					TRES_PUT (c == '\n' ? 'n' : c == '\t' ? 't' : c == '\r' ? 'r' : '\\');
				}
				else
					TRES_PUT (c);
			}
			o++;
		}
		TRES_PUT ('\n');
	}
#undef TRES_PUT
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT TRES:%s (%u records) -> %s/text.txt\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n, dest);
	enumError err = ERR_OK;
	if (!testmode)
		err = shade_write (arg, dest, basedir, "text.txt", out, (uint)len);
	FREE (out);
	FREE (raw);
	return err;
}
