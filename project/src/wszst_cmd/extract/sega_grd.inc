// SEGA "MPLN" 2D effect/graphics sets (Sakura Wars: So Long, My Love "MARS/GR/**/*.GRD", little endian): a chunk {"MPLN", u32 size without header, u32 header size 0x10, 0} whose body (at 0x10) starts with a
// 0x30 byte header {u32 palette offset, u32 sub-chunk offset, u16 17, u16 1, u16 width, u16 height, 6 floats, u32 flags (&0xf00: 0x100/0x800 = 16, 0x200/0x900 = 256 colours), u32 palette bytes}, then the palette (RGBA
// bytes, alpha 0..0x80) and the sub-chunks (each {magic, u32 size without header, u32 header size 0x10, flags}, stride size + 0x10): MDCG cells (header word 5 = offset of the cell's vertex list start / "HTEX" chunk,
// vertex lists are GX data that is not decoded) and MNCA tables {3 words, count, count x 4 u16 rectangles}. An HTEX chunk (flags 0x400) is Melt compressed (see sega_melt.inc) to an "HTSF" texture container
// ("PVRT" blocks, pixel layout not decoded); it is written raw as "<name>.<cell>.htsf". The JSON carries the header fields, the palette, the MNCA rectangles and the cell sizes.
static enumError extract_sega_grd_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".GRD") && !is_ext (arg, ".grd"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x80 || memcmp (c, "MPLN", 4) || rd_le32 (c + 8) != 0x10)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	const u8 *body = c + 0x10;
	const size_t bs = cs - 0x10;
	const u32 poff = rd_le32 (body), soff = rd_le32 (body + 4), pbytes = rd_le32 (body + 0x2c);
	if (!pbytes || pbytes > 0x400 || (u64)poff + pbytes > bs || soff > bs)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "{\n\t\"format\": \"Sakura Wars MPLN\",\n\t\"width\": %u, \"height\": %u, \"flags\": %u,\n\t\"palette\": \"", rd_le16 (body + 0xc), rd_le16 (body + 0xe), rd_le32 (body + 0x28));
	for (u32 i = 0; i < pbytes; i++)
		sumo_printf (&b, "%02x", body[poff + i]);
	sumo_printf (&b, "\",\n\t\"cells\": [");
	uint ncell = 0;
	u8 *dec = MALLOC (0x1000000);
	sumo_buf_t rects = { 0, 0, 0, true };
	char dest[PATH_MAX], name[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	const char *sl = strrchr (arg, '/');
	snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
	char *dot = strrchr (name, '.');
	if (dot)
		*dot = 0;
	enumError err = ERR_OK;
	for (size_t p = soff; p + 0x10 <= bs;)
	{
		const u8 *ch = body + p;
		const u32 sz = rd_le32 (ch + 4);
		if (p + 0x10 + (size_t)sz > bs)
			break;
		if (!memcmp (ch, "MDCG", 4) && sz > 0x30)
		{
			const u32 ho = 0x10 + rd_le32 (ch + 0x10 + 0x14);
			size_t n = 0;
			bool packed = false;
			if (ho + 0x20 <= sz + 0x10 && !memcmp (ch + ho, "HTEX", 4) && (rd_le32 (ch + ho + 0xc) & 0x400))
			{
				const u32 hsz = rd_le32 (ch + ho + 4);
				packed = ho + hsz <= sz && sega_melt (ch + ho, (size_t)hsz + 0x10, 0x20, dec, 0x1000000, &n) && n > 0x30 && !memcmp (dec, "HTSF", 4);
			}
			sumo_printf (&b, "%s\n\t\t{\"mdcg_size\": %u, \"htex_offset\": %u, \"htsf_bytes\": %zu}", ncell ? "," : "", sz, ho, packed ? n : 0);
			if (packed && !testmode)
			{
				char fn[PATH_MAX];
				snprintf (fn, sizeof (fn), "%s.%u.htsf", name, ncell);
				if (verbose >= 0)
					fprintf (stdlog, "EXTRACT SEGA-GRD:%s (HTSF %zu bytes) -> %s/%s\n", arg, n, dest, fn);
				const enumError e = shade_write (arg, dest, basedir, fn, dec, (uint)n);
				if (e)
					err = e;
			}
			ncell++;
		}
		else if (!memcmp (ch, "MNCA", 4) && sz >= 0x10)
		{
			const u32 cnt = rd_le32 (ch + 0x10 + 8);
			if ((u64)cnt * 8 + 12 <= sz)
				for (u32 i = 0; i < cnt; i++)
					sumo_printf (&rects, "%s[%u, %u, %u, %u]", i ? ", " : "", rd_le16 (ch + 0x1c + 8 * i), rd_le16 (ch + 0x1e + 8 * i), rd_le16 (ch + 0x20 + 8 * i), rd_le16 (ch + 0x22 + 8 * i));
		}
		p += 0x10 + (size_t)sz;
		if (!memcmp (ch, "EOFC", 4))
			break;
	}
	sumo_printf (&b, "\n\t],\n\t\"rects\": [%s]\n}\n", rects.p ? rects.p : "");
	FREE (dec);
	FREE (rects.p);
	FREE (c);
	if (!err && b.ok && b.n && b.n < 0xffffffffu)
	{
		char jn[PATH_MAX];
		snprintf (jn, sizeof (jn), "%s.json", name);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SEGA-GRD:%s (%u cells) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ncell, dest, jn);
		err = shade_write (arg, dest, basedir, jn, (u8 *)b.p, (uint)b.n);
	}
	else if (!err)
		err = ERR_NOTHING_TO_DO;
	FREE (b.p);
	return err;
}
