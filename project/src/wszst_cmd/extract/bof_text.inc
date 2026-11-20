// Battle of Giants: Dinosaurs Strike (Wii) language tables (".bof", one per language; little endian). Layout: u32 version (1), u32 string count N, u32 UTF-16 unit count C, C units of UTF-16LE holding
// exactly N NUL terminated strings (speaker names and abbreviations, then {text key "txt_TRex", voice id "VO_CHA_001", text} runs), followed by an index table of 12 byte records {u32 unit offset,
// u32 id, u32 1} (whose offsets refer to the English layout) and a tail the extractor leaves raw. Written as "<file>.json" with every string in order and the index records.
static void bof_json_text (sumo_buf_t *b, const u8 *p, size_t units)
{
	for (size_t i = 0; i < units; i++)
	{
		const uint u = rd_le16 (p + 2 * i);
		if (u == '"' || u == '\\')
			sumo_printf (b, "\\%c", (int)u);
		else if (u >= 0x20 && u < 0x7f)
			sumo_printf (b, "%c", (int)u);
		else
			sumo_printf (b, "\\u%04x", u);
	}
}

static enumError extract_bof_text_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".bof"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x20 || rd_le32 (c) != 1)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	const u32 n = rd_le32 (c + 4), units = rd_le32 (c + 8);
	if (!n || n > 1000000 || !units || units > cs || 12 + 2ull * units > cs || rd_le16 (c + 12 + 2 * (size_t)(units - 1)))
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	u32 nul = 0;
	for (u32 i = 0; i < units; i++)
		nul += !rd_le16 (c + 12 + 2 * (size_t)i);
	if (nul != n)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "{\n\t\"format\": \"Battle of Giants BOF text\",\n\t\"strings\": [");
	u32 start = 0, k = 0;
	for (u32 i = 0; i < units; i++)
		if (!rd_le16 (c + 12 + 2 * (size_t)i))
		{
			sumo_printf (&b, "%s\n\t\t\"", k++ ? "," : "");
			bof_json_text (&b, c + 12 + 2 * (size_t)start, i - start);
			sumo_printf (&b, "\"");
			start = i + 1;
		}
	sumo_printf (&b, "\n\t],\n\t\"index\": [");
	const size_t t = 12 + 2 * (size_t)units;
	const size_t nr = (cs - t) / 12;
	for (size_t r = 0; r < nr; r++)
		sumo_printf (&b, "%s[%u, %u, %u]", r ? ", " : "", rd_le32 (c + t + 12 * r), rd_le32 (c + t + 12 * r + 4), rd_le32 (c + t + 12 * r + 8));
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
			*dot = 0;
		strncat (name, ".json", sizeof (name) - strlen (name) - 1);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT BOF-TEXT:%s (%u strings) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
