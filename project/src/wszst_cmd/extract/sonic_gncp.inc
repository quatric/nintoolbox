// Sonic Team / Sega Ninja "NGIF" UI layout files (Sonic Colors, Wii: ".gncp", 164 files of the retail disc; the chunk sizes are little endian, everything else big endian). Layout: "NGIF" + u32 LE size 0x18 +
// {u32 0x01000000-style flags, u32 0x20 data start, u32 offset of the end of the data, u32 offset of the "NOF0" chunk, u32 size, u32 1}, the data chunk ("nCPJ", u32 LE size) at 0x20, then the "NOF0"
// pointer list {"NOF0", u32 LE size, u32 count, 0, count u32 offsets of pointer slots, relative to 0x20} and "NEND". Every slot holds an offset relative to 0x20. Written as "<file>.json": the data
// words, the pointer slot offsets and the names the pointers lead to (layer / cast names such as "Root", "bg_block").
static enumError extract_sonic_gncp_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".gncp"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x60 || memcmp (c, "NGIF", 4))
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	const size_t nof = rd_be32 (c + 0x14);
	bool ok = nof >= 0x28 && !(nof & 3) && nof + 0x10 <= cs && !memcmp (c + nof, "NOF0", 4);
	const u32 cnt = ok ? rd_be32 (c + nof + 8) : 0;
	ok = ok && cnt <= 0x100000 && nof + 0x10 + 4ull * cnt <= cs;
	const size_t dend = nof;
	for (u32 i = 0; ok && i < cnt; i++)
	{
		const size_t o = rd_be32 (c + nof + 0x10 + 4 * (size_t)i);
		ok = !(o & 3) && 0x20 + o + 4 <= dend && rd_be32 (c + 0x20 + o) <= dend - 0x20;
	}
	if (!ok)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "{\n\t\"format\": \"Sonic Team NGIF layout\",\n\t\"words\": [");
	for (size_t k = 0; 0x20 + 4 * k + 4 <= dend; k++)
		sumo_printf (&b, "%s%u", k ? ", " : "", rd_be32 (c + 0x20 + 4 * k));
	sumo_printf (&b, "],\n\t\"pointers\": [");
	for (u32 i = 0; i < cnt; i++)
		sumo_printf (&b, "%s%u", i ? ", " : "", rd_be32 (c + nof + 0x10 + 4 * (size_t)i));
	sumo_printf (&b, "],\n\t\"strings\": [");
	uint ns = 0;
	for (u32 i = 0; i < cnt; i++)
	{
		const size_t slot = rd_be32 (c + nof + 0x10 + 4 * (size_t)i), to = rd_be32 (c + 0x20 + slot);
		size_t n = 0;
		while (0x20 + to + n < dend && n < 200 && c[0x20 + to + n] >= 0x20 && c[0x20 + to + n] < 0x7f)
			n++;
		if (n > 1 && 0x20 + to + n < dend && !c[0x20 + to + n])
			sumo_printf (&b, "%s{\"slot\": %zu, \"offset\": %zu, \"text\": \"%.*s\"}", ns++ ? ", " : "", slot, to, (int)n, (ccp)c + 0x20 + to);
	}
	sumo_printf (&b, "]\n}\n");
	FREE (c);
	enumError err = ERR_NOTHING_TO_DO;
	if (b.ok && b.n && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".json");
		else
			strcat (name, ".json");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SONIC-GNCP:%s (%u pointers, %u strings) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, cnt, ns, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
