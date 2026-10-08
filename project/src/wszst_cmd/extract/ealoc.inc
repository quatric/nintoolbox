// EA "LOCH"/"LOCL" locale string tables (Madden NFL ".loc"): "LOCH", u32 0x14,
// u32 0, u32 1, u32 0x14, then the "LOCL" chunk at 0x14 {"LOCL", u32 size, u32 0,
// u32 offset[] at +0xc}. Offsets are relative to the chunk; entry 0 is the start of a
// pointer table that also ends the offset list, entries 1.. point at NUL-terminated
// UTF-16LE strings. Written as strings.txt ("<index>\t<text>", newlines escaped).
static enumError extract_ealoc_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x30 || memcmp (raw, "LOCH", 4) || memcmp (raw + 0x14, "LOCL", 4) || rd_le32 (raw + 4) != 0x14)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u8 *chunk = raw + 0x14;
	const size_t csz = sz - 0x14;
	const size_t tbl_end = rd_le32 (chunk + 0xc);
	if (tbl_end < 0x14 || tbl_end > csz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = (uint)((tbl_end - 0xc) / 4);
	size_t cap = csz * 3 + 32 * (size_t)count + 64, used = 0;
	char *out = MALLOC (cap);
	if (!out)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	for (uint i = 1; i < count; i++)
	{
		const size_t o = rd_le32 (chunk + 0xc + 4 * (size_t)i);
		if (o < tbl_end || o + 2 > csz)
			continue;
		used += snprintf (out + used, cap - used, "%u\t", i);
		for (size_t p = o; p + 2 <= csz && used + 8 < cap; p += 2)
		{
			const uint c = chunk[p] | (chunk[p + 1] << 8);
			if (!c)
				break;
			if (c == '\n' || c == '\r')
				out[used++] = '\\', out[used++] = c == '\n' ? 'n' : 'r';
			else if (c < 0x80)
				out[used++] = (char)c;
			else if (c < 0x800)
				out[used++] = (char)(0xc0 | (c >> 6)), out[used++] = (char)(0x80 | (c & 0x3f));
			else
				out[used++] = (char)(0xe0 | (c >> 12)), out[used++] = (char)(0x80 | ((c >> 6) & 0x3f)),
				out[used++] = (char)(0x80 | (c & 0x3f));
		}
		out[used++] = '\n';
		n++;
	}
	FREE (raw);
	if (!n)
	{
		FREE (out);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT EA LOC:%s (%u strings) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n, dest);
	enumError err = shade_write (arg, dest, basedir, "strings.txt", (u8 *)out, (uint)used);
	FREE (out);
	return err;
}
