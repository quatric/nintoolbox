// Igor: The Game bitmap font metrics (".fbin" / ".sfbin" in fonts/<name>/, little endian, the file is a stream of u32 values at any byte offset): the signature "FRITE" as five u32 characters,
// {u32 18, u32 1, u32 name length, name bytes (e.g. "fonts/Vera/vera\0")}, {u32 sprite count N, u32 0}, N sprite rectangles {s32 x, y, width, height, flags} into the font texture (".sfbin": {index, 0}), then
// character blocks until the end of the file: {u32 first code point, u32 count} followed by count characters {u32 advance, u32 sprite count, per sprite {u32 sprite index, s32 x offset, s32 y
// offset}}. The same records appear as text in the "Vera-*.txt" files next to the fonts (all 107 character lines agree). Written as "<file>.json".
static enumError extract_igor_fbin_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".fbin") && !is_ext (arg, ".sfbin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 48)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint rs = is_ext (arg, ".sfbin") ? 8 : 20; // ".sfbin": rectangles are {index, 0}
	static const u8 sig[5] = { 'F', 'R', 'I', 'T', 'E' };
	bool ok = true;
	for (uint i = 0; i < 5; i++)
		ok = ok && rd_le32 (raw + 4 * i) == sig[i];
	const u32 nl = ok ? rd_le32 (raw + 28) : 0;
	size_t p = 32 + (size_t)nl;
	ok = ok && nl && nl < 256 && p + 8 <= sz && !raw[p - 1];
	const u32 n = ok ? rd_le32 (raw + p) : 0;
	ok = ok && n && n < 100000 && rd_le32 (raw + p + 4) == 0 && p + 8 + (size_t)rs * n <= sz;
	if (!ok)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "{\n\t\"format\": \"Igor font\",\n\t\"name\": \"%s\",\n\t\"sprites\": [", (ccp)raw + 32);
	const size_t rp = p + 8;
	for (u32 i = 0; i < n; i++)
	{
		sumo_printf (&b, "%s[", i ? ", " : "");
		for (uint k = 0; k < rs / 4; k++)
			sumo_printf (&b, "%s%d", k ? ", " : "", (s32)rd_le32 (raw + rp + (size_t)rs * i + 4 * k));
		sumo_printf (&b, "]");
	}
	sumo_printf (&b, "],\n\t\"glyphs\": [");
	p = rp + (size_t)rs * n;
	uint ng = 0;
	while (ok && p + 8 <= sz)
	{
		const u32 start = rd_le32 (raw + p), cnt = rd_le32 (raw + p + 4);
		p += 8;
		if (cnt > 0x10000)
		{
			ok = false;
			break;
		}
		for (u32 i = 0; i < cnt && ok; i++)
		{
			if (p + 8 > sz)
			{
				ok = false;
				break;
			}
			const u32 adv = rd_le32 (raw + p), ns = rd_le32 (raw + p + 4);
			if (ns > 64 || p + 8 + 12ull * ns > sz)
			{
				ok = false;
				break;
			}
			sumo_printf (&b, "%s\n\t\t{\"code\": %u, \"advance\": %d, \"sprites\": [", ng++ ? "," : "", start + i, (s32)adv);
			for (u32 k = 0; k < ns; k++)
				sumo_printf (&b, "%s[%u, %d, %d]", k ? ", " : "", rd_le32 (raw + p + 8 + 12 * k), (s32)rd_le32 (raw + p + 12 + 12 * k), (s32)rd_le32 (raw + p + 16 + 12 * k));
			sumo_printf (&b, "]}");
			p += 8 + 12 * (size_t)ns;
		}
	}
	sumo_printf (&b, "\n\t]\n}\n");
	ok = ok && p == sz;
	FREE (raw);
	enumError err = ERR_NOTHING_TO_DO;
	if (ok && b.ok && b.n < 0xffffffffu)
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
			fprintf (stdlog, "%s%sEXTRACT IGOR-FONT:%s (%u glyphs, %u sprites) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ng, n, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
