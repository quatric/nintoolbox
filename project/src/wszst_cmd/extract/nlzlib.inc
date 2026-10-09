// Next Level Games ".zlib" wrapper (Mario Strikers Charged: "*.sanim.zlib"):
// u32 big-endian decompressed size followed by a plain zlib stream. The payload is
// written to "<file>.d/<file without .zlib>".
static enumError extract_nlzlib_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".zlib"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t out_size = sz > 6 ? rd_be32 (raw) : 0;
	if (!out_size || out_size > (size_t)256 << 20 || raw[4] != 0x78)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	u8 *out = MALLOC (out_size);
	uLongf dsz = out_size;
	if (!out || uncompress (out, &dsz, raw + 4, sz - 4) != Z_OK || dsz != out_size)
	{
		FREE (out);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	FREE (raw);
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	char name[PATH_MAX];
	snprintf (name, sizeof (name), "%s", base);
	name[strlen (name) - 5] = 0;
	if (!*name)
		snprintf (name, sizeof (name), "data.bin");
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT NL-ZLIB:%s (%zu bytes) -> %s/%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, out_size, dest, name);
	const enumError err = shade_write (arg, dest, basedir, name, out, (uint)out_size);
	FREE (out);
	return err;
}

// Next Level Games "NLOC" locale tables (".loc"): big-endian {"NLOC", u32 1, u32 hash,
// u32 count, u32 1, {u32 id, u32 offset}[count]} followed by the UTF-16BE NUL-terminated
// string pool; offsets count 16-bit characters. Written as strings.txt ("<id>\t<text>").
static enumError extract_nloc_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 0x20 ? rd_be32 (raw + 0x0c) : 0;
	if (sz < 0x20 || memcmp (raw, "NLOC", 4) || rd_be32 (raw + 4) != 1 || !count || count > 0x100000
		|| 0x14 + 8 * (size_t)count > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t pool = 0x14 + 8 * (size_t)count;
	size_t cap = (sz - pool) * 3 / 2 + 32 * (size_t)count + 64, used = 0;
	char *out = MALLOC (cap);
	if (!out)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	for (uint i = 0; i < count; i++)
	{
		const u32 id = rd_be32 (raw + 0x14 + 8 * (size_t)i), off = rd_be32 (raw + 0x18 + 8 * (size_t)i);
		if (pool + 2 * (size_t)off + 2 > sz)
			continue;
		used += snprintf (out + used, cap - used, "%u\t", id);
		for (size_t p = pool + 2 * (size_t)off; p + 2 <= sz && used + 8 < cap; p += 2)
		{
			const uint c = (raw[p] << 8) | raw[p + 1];
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
		fprintf (stdlog, "%s%sEXTRACT NLOC:%s (%u strings) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n, dest);
	const enumError err = shade_write (arg, dest, basedir, "strings.txt", (u8 *)out, (uint)used);
	FREE (out);
	return err;
}
