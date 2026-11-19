// SEGA "GLIP" lip-sync tables (Sakura Wars: So Long, My Love "*.cnk", little endian), structure after the 41 retail files: {"GLIP", u32 file size, u32 header size 0x10, 0}, u32 entry count, u32 8,
// then count 8-byte entries {u32 key, u32 record offset}; keys are sorted (0x16000 + n for the first ones, voice ids such as 0x1f7ee later) and the offsets are relative to 0x10 (the count field);
// a record runs to the next offset. The records are short byte streams (14, 12, 11 ... bytes) whose coding is not decoded. Written as JSON with the key and the record bytes in hex.
static enumError extract_sega_glip_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".cnk"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x20 || memcmp (c, "GLIP", 4) || rd_le32 (c + 8) != 0x10 || rd_le32 (c + 0x14) != 8)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	const size_t n = rd_le32 (c + 0x10);
	const size_t tab = 0x18, data = 0x10;
	bool ok = n && n < 0x100000 && tab + 8 * n <= cs;
	for (size_t i = 0; ok && i < n; i++)
	{
		const size_t o = data + rd_le32 (c + tab + 8 * i + 4), e = i + 1 < n ? data + rd_le32 (c + tab + 8 * i + 12) : cs;
		ok = o >= tab + 8 * n && o <= e && e <= cs;
	}
	if (!ok)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "{\n\t\"format\": \"Sakura Wars GLIP\",\n\t\"entries\": [");
	for (size_t i = 0; i < n; i++)
	{
		const size_t o = data + rd_le32 (c + tab + 8 * i + 4), e = i + 1 < n ? data + rd_le32 (c + tab + 8 * i + 12) : cs;
		sumo_printf (&b, "%s\n\t\t{\"key\": %u, \"data\": \"", i ? "," : "", rd_le32 (c + tab + 8 * i));
		for (size_t k = o; k < e; k++)
			sumo_printf (&b, "%02x", c[k]);
		sumo_printf (&b, "\"}");
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
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SEGA-GLIP:%s (%zu entries) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
