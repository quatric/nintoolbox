// SEGA "ABDA" blocks (Sakura Wars: So Long, My Love "*.abd", "*.arm", "*.bfX", "*.bf1", "*.dia" and ABDA members of URES/AM?? packs; 670 retail files; little endian), structure: {"ABDA", u32 size, u32
// header size (0x30..0x140), 0}, u32 type hash (0x77820d59 in most files), u32 section count, u32 total section bytes, then from 0x20 a list of 8-byte entries {u32 section offset, 0}. Sections start
// with {tag, u32 payload size, u32 header size 0x10, 0}: "ABDT" mostly. An ABDT payload starts {u32 hash, u32 0x40, u32 -1, f32 -1, f32 -1, ...} followed by offset tables to sub-blocks; the object
// graph below the section level (animation / effect curves) is not decoded. The ABDT block table (u32 count at +0x34, then count+1 offsets relative to the section start) is listed as "blocks" (offset, size). Written as JSON with the section tag, offset, size and the first 64 payload words of every section.
static enumError extract_sega_abda_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!file_has_magic (arg, "ABDA", 4))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x30)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	const size_t hs = rd_le32 (c + 8), n = rd_le32 (c + 0x14);
	bool ok = hs >= 0x30 && hs <= 0x400 && hs < cs && n && n <= 4096 && 0x20 + 8 * n <= hs;
	size_t prev = 0;
	for (size_t i = 0; ok && i < n; i++)
	{
		const size_t o = rd_le32 (c + 0x20 + 8 * i);
		ok = o >= hs && o + 0x10 <= cs && o >= prev;
		prev = o;
	}
	if (!ok)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "{\n\t\"format\": \"Sakura Wars ABDA\",\n\t\"hash\": %u,\n\t\"sections\": [", rd_le32 (c + 0x10));
	for (size_t i = 0; i < n; i++)
	{
		const size_t o = rd_le32 (c + 0x20 + 8 * i), e = o + rd_le32 (c + o + 8) + rd_le32 (c + o + 4);
		const size_t end = e > o && e <= cs ? e : cs;
		sumo_printf (&b, "%s\n\t\t{\"tag\": \"%.4s\", \"offset\": %zu, \"size\": %zu, \"words\": [", i ? "," : "", (ccp)c + o, o, end - o);
		for (size_t w = o + 0x10; w + 4 <= end && w < o + 0x10 + 256; w += 4)
			sumo_printf (&b, "%s%u", w > o + 0x10 ? ", " : "", rd_le32 (c + w));
		sumo_printf (&b, "]");
		// ABDT block table: u32 block count at +0x34, then count offsets (relative to the section start) of the sub-blocks and one more offset that ends the last block
		if (!memcmp (c + o, "ABDT", 4) && o + 0x38 <= end)
		{
			const u32 m = rd_le32 (c + o + 0x34);
			bool good = m && m <= 64 && o + 0x38 + 4 * (size_t)(m + 1) <= end;
			for (u32 k = 0; k <= m && good; k++)
			{
				const u32 v = rd_le32 (c + o + 0x38 + 4 * k);
				good = o + v <= end && (!k || v >= rd_le32 (c + o + 0x38 + 4 * (k - 1))) && v >= 0x38 + 4 * (m + 1);
			}
			if (good)
			{
				sumo_printf (&b, ", \"blocks\": [");
				for (u32 k = 0; k < m; k++)
				{
					const u32 v = rd_le32 (c + o + 0x38 + 4 * k), v2 = rd_le32 (c + o + 0x38 + 4 * (k + 1));
					sumo_printf (&b, "%s{\"offset\": %u, \"size\": %u}", k ? ", " : "", v, v2 - v);
				}
				sumo_printf (&b, "]");
			}
		}
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
			fprintf (stdlog, "%s%sEXTRACT SEGA-ABDA:%s (%zu sections) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
