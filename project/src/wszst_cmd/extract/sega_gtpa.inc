// SEGA "GTPA" text page tables (Sakura Wars: So Long, My Love "*.gtp" and some "*.arm", little endian), structure after the 17 retail files: {"GTPA", u32 file size, u32 header size 0x10, 0}, u32 record
// count, count u32 record offsets (relative to 0x10; a record runs to the next offset). A record is a page layout followed by its text: 24 words of layout data at the start (the first words are an id,
// a length, a style; f32 values at +0x10 are position/size such as 180, 640, 448, +0x1c is an RGBA colour, +0x20.. glyph sizes), and from +0x60 the NUL-terminated message text (English in the US
// disc; "\n" separates lines). Written as JSON with the layout words (not interpreted) and the text.
static void gtpa_json_str (sumo_buf_t *b, const u8 *s, size_t n)
{
	sumo_printf (b, "\"");
	for (size_t i = 0; i < n && s[i]; i++)
	{
		const uint c = s[i];
		if (c == '"' || c == '\\')
			sumo_printf (b, "\\%c", c);
		else if (c == '\n')
			sumo_printf (b, "\\n");
		else if (c < 0x20 || c >= 0x7f)
			sumo_printf (b, "\\u%04x", c);
		else
			sumo_printf (b, "%c", c);
	}
	sumo_printf (b, "\"");
}

static enumError extract_sega_gtpa_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!file_has_magic (arg, "GTPA", 4))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x20 || rd_le32 (c + 8) != 0x10)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	const size_t n = rd_le32 (c + 0x10), data = 0x10;
	bool ok = n && n < 0x10000 && 0x14 + 4 * n <= cs;
	for (size_t i = 0; ok && i < n; i++)
	{
		const size_t o = data + rd_le32 (c + 0x14 + 4 * i), e = i + 1 < n ? data + rd_le32 (c + 0x14 + 4 * i + 4) : cs;
		ok = o >= 0x14 + 4 * n && o + 0x60 <= e && e <= cs;
	}
	if (!ok)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "{\n\t\"format\": \"Sakura Wars GTPA\",\n\t\"pages\": [");
	for (size_t i = 0; i < n; i++)
	{
		const size_t o = data + rd_le32 (c + 0x14 + 4 * i), e = i + 1 < n ? data + rd_le32 (c + 0x14 + 4 * i + 4) : cs;
		sumo_printf (&b, "%s\n\t\t{\"index\": %zu, \"layout_words\": [", i ? "," : "", i);
		for (uint w = 0; w < 24; w++)
			sumo_printf (&b, "%s%u", w ? ", " : "", rd_le32 (c + o + 4 * w));
		sumo_printf (&b, "], \"text\": ");
		gtpa_json_str (&b, c + o + 0x60, e - o - 0x60);
		sumo_printf (&b, "}");
	}
	sumo_printf (&b, "\n\t]\n}\n");
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
			fprintf (stdlog, "%s%sEXTRACT SEGA-GTPA:%s (%zu pages) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
