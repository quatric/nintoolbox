// SPHC / Storm City Games locale string table ".txf" (Gummy Bears MiniGolf,
// Pool Hall Pro, Vertigo, ...): little-endian {u32 count, u32 offset[count]}
// followed by NUL-terminated UTF-8 strings. Written as strings.txt, one
// "<index>\t<text>" line per string (newlines escaped as \n).
static enumError extract_spooktxf_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".txf"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 8 ? rd_le32 (raw) : 0;
	if (!count || count > 100000 || 4 + 4 * (size_t)count > sz || rd_le32 (raw + 4) != 4 + 4 * count)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	uint prev = 0;
	for (uint i = 0; i < count; i++)
	{
		const uint o = rd_le32 (raw + 4 + 4 * i);
		if (o < prev || o >= sz)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		prev = o;
	}
	size_t cap = sz * 2 + 64 * (size_t)count, used = 0;
	char *out = MALLOC (cap);
	if (!out)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	for (uint i = 0; i < count; i++)
	{
		const uint o = rd_le32 (raw + 4 + 4 * i);
		used += snprintf (out + used, cap - used, "%u\t", i);
		for (const u8 *p = raw + o; p < raw + sz && *p && used + 4 < cap; p++)
		{
			if (*p == '\n')
				out[used++] = '\\', out[used++] = 'n';
			else if (*p == '\r')
				out[used++] = '\\', out[used++] = 'r';
			else
				out[used++] = (char)*p;
		}
		out[used++] = '\n';
	}
	FREE (raw);
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT TXF:%s (%u strings) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);
	enumError err = shade_write (arg, dest, basedir, "strings.txt", (u8 *)out, (uint)used);
	FREE (out);
	return err;
}
