// Sengoku Basara 3 (Wii) UI layout files: ".lsp" ("\0PSL", u32be version 33) and animation scripts ".sdl"
// ("\0LDS"). The node and property records are not decoded; <name>.d/strings.txt lists the names stored in
// them ("offset<TAB>text": node names, animation names, SDL property names such as mSeId, and the texture
// paths an .lsp refers to). A length byte in front of a string (an .lsp path) is dropped.
static enumError extract_basaraui_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	const bool lsp = is_ext (arg, ".lsp"), sdl = is_ext (arg, ".sdl");
	if (!lsp && !sdl)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x30 || raw[0] || memcmp (raw + 1, lsp ? "PSL" : "LDS", 3) || (lsp && rd_be32 (raw + 4) != 33))
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
	used += snprintf (out, cap, "# %s, %zu bytes\n", lsp ? "LSP layout (PSL, version 33)" : "SDL animation script (LDS)", sz);
	uint n = 0;
	for (size_t o = 0x20; o < sz;)
	{
		size_t e = o;
		while (e < sz && raw[e] >= 0x20 && raw[e] < 0x7f)
			e++;
		size_t b = o;
		if (e - o == raw[o] && e - o > 3 && e < sz && !raw[e])
			b++;
		if (e - b >= 3 && (e == sz || !raw[e]) && used + (e - b) + 24 < cap)
		{
			used += snprintf (out + used, cap - used, "%zu\t", b);
			memcpy (out + used, raw + b, e - b);
			used += e - b;
			out[used++] = '\n';
			n++;
		}
		o = e + 1;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BASARA-%s:%s (%u strings) -> %s/strings.txt\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", lsp ? "LSP" : "SDL", arg, n, dest);
	const enumError err = shade_write (arg, dest, basedir, "strings.txt", (u8 *)out, (uint)used);
	FREE (out);
	FREE (raw);
	return err;
}
