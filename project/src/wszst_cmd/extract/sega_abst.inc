// SEGA "ABST" asset schedules (Sakura Wars: So Long, My Love "*.asd", little endian), structure after the 47 retail files: {"ABST", u32 file size, u32 header size (0x30..0x1a0, a multiple of 16), 0, u32 7, u32 0x24, u32 3, u32
// 0x40, 0}, then from 0x24 a zero-terminated list of u32 chunk offsets (sorted, the first is the header size). Chunks are {tag, u32, u32, 0}: "ASAD" asset descriptors (the 32-byte name at +0x10 is
// the asset: evmap, light_ev, fog, glare_ev, GZR_EFEV... effect scripts) and "ASSC" schedule blocks. Each chunk is written as JSON with its tag, offset, size, name (ASAD) and the payload as u32 words;
// the meaning of the numeric fields is not known.
static enumError extract_sega_abst_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".asd"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x30 || memcmp (c, "ABST", 4) || rd_le32 (c + 8) < 0x30 || rd_le32 (c + 8) > cs || rd_le32 (c + 8) > 0x400)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	u32 off[64];
	uint n = 0;
	const size_t hs = rd_le32 (c + 8);
	for (size_t p = 0x24; p + 4 <= hs && n < 63; p += 4)
	{
		const u32 o = rd_le32 (c + p);
		if (!o)
			break;
		if (o < hs || o + 0x10 > cs || (n && o <= off[n - 1]))
		{
			n = 0;
			break;
		}
		off[n++] = o;
	}
	if (!n)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	off[n] = (u32)cs;
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "{\n\t\"format\": \"Sakura Wars ABST\",\n\t\"chunks\": [");
	for (uint i = 0; i < n; i++)
	{
		const u8 *k = c + off[i];
		const u32 size = off[i + 1] - off[i];
		sumo_printf (&b, "%s\n\t\t{\"tag\": \"%.4s\", \"offset\": %u, \"size\": %u", i ? "," : "", (ccp)k, off[i], size);
		size_t w0 = 0x10;
		if (!memcmp (k, "ASAD", 4) && size >= 0x30)
		{
			sumo_printf (&b, ", \"name\": \"%.32s\"", (ccp)k + 0x10);
			w0 = 0x30;
		}
		sumo_printf (&b, ", \"words\": [");
		for (size_t w = w0; w + 4 <= size && w < w0 + 1024; w += 4)
			sumo_printf (&b, "%s%u", w > w0 ? ", " : "", rd_le32 (k + w));
		sumo_printf (&b, "]}");
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
			fprintf (stdlog, "%s%sEXTRACT SEGA-ABST:%s (%u chunks) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
