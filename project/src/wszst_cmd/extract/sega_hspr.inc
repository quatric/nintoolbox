// SEGA "HSPR" sprite sheets (Sakura Wars: So Long, My Love; ".hspr" members of URES / AM?? packs and "*.bfX" / "*.arm" files; 519 sheets checked; little endian), container level: {"HSPR", u32 size, u32
// header size 0x10, 0} wraps a "KSPR" chunk at 0x10 (payload at 0x20, all offsets are relative to the HSPR chunk): {u32 version 1, 0, u32 N, u32 0x30}, at 0x30 {u32 sprite count, u32 offset of the sprite
// records, u32 count again, u32 offset of a second table (one 48-byte record per sprite: 0, 0x30, index, offset of the sprite record)}. The sprite records start 0x10 bytes after the table offset (the
// 16 bytes before them hold a float rectangle) and are 32 bytes each: {u32 index (0..count-1), 0, u32 kind (1..10), 0, f32 width, f32 height, u32 offset A, u32 offset B}; A and B point at GX command
// streams (texture coordinates / quads) that are not decoded. Written as JSON with the sprite table.
static enumError extract_sega_hspr_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!file_has_magic (arg, "HSPR", 4))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x60 || rd_le32 (c + 8) != 0x10 || memcmp (c + 0x10, "KSPR", 4))
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	const size_t n = rd_le32 (c + 0x30), t1 = rd_le32 (c + 0x34);
	bool ok = n && n <= 0x10000 && rd_le32 (c + 0x38) == n && t1 + 0x10 + 0x20 * n <= cs;
	for (size_t i = 0; ok && i < n; i++)
		ok = rd_le32 (c + t1 + 0x10 + 0x20 * i) == i;
	if (!ok)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "{\n\t\"format\": \"Sakura Wars HSPR\",\n\t\"sprites\": [");
	for (size_t i = 0; i < n; i++)
	{
		const u8 *r = c + t1 + 0x10 + 0x20 * i;
		float w, h;
		{
			const u32 a = rd_le32 (r + 0x10), bb = rd_le32 (r + 0x14);
			memcpy (&w, &a, 4);
			memcpy (&h, &bb, 4);
		}
		sumo_printf (&b, "%s\n\t\t{\"index\": %zu, \"kind\": %u, \"width\": %.9g, \"height\": %.9g, \"offset_a\": %u, \"offset_b\": %u}", i ? "," : "", i, rd_le32 (r + 8),
			(double)w, (double)h, rd_le32 (r + 0x18), rd_le32 (r + 0x1c));
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
			fprintf (stdlog, "%s%sEXTRACT SEGA-HSPR:%s (%zu sprites) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
