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

// Face textures ("PVRT" kind 0x6c, pixel format 9, 128 x 128): a 1024 byte RGBA palette (alpha 0..0x80) followed by 16384 index bytes in a swizzled order. With i the byte index, the stored position is
// x = i8 | i0 << 1 | i9 << 2 | i10 << 3 | i11 << 4 | i12 << 5 | i13 << 6 and y = i2 | i3 << 1 | (i0 ^ i4 ^ i9) << 2 | i1 << 3 | i5 << 4 | i6 << 5 | i7 << 6 (found by searching bit assignments for the
// smoothest image and checked on the four retail faces); the picture is then rotated a quarter turn: pixel (127 - y, x). Returns the RGBA image or NULL.
static u8 *sega_face_rgba (const u8 *htsf, size_t n)
{
	size_t b = 0x10;
	if (b + 0x10 <= n && !memcmp (htsf + b, "GBIX", 4))
		b += 0x10;
	if (b + 0x10 + 0x400 + 0x4000 > n || memcmp (htsf + b, "PVRT", 4) || htsf[b + 8] != 9 || htsf[b + 9] != 0x6c || rd_le16 (htsf + b + 0xc) != 128 || rd_le16 (htsf + b + 0xe) != 128)
		return 0;
	const u8 *pal = htsf + b + 0x10, *px = pal + 0x400;
	u8 *rgba = MALLOC (128 * 128 * 4);
	for (uint i = 0; i < 0x4000; i++)
	{
		#define SB(k) (((i) >> (k)) & 1u)
		const uint x = SB (8) | SB (0) << 1 | SB (9) << 2 | SB (10) << 3 | SB (11) << 4 | SB (12) << 5 | SB (13) << 6;
		const uint y = SB (2) | SB (3) << 1 | (SB (0) ^ SB (4) ^ SB (9)) << 2 | SB (1) << 3 | SB (5) << 4 | SB (6) << 5 | SB (7) << 6;
		#undef SB
		u8 *o = rgba + ((size_t)x * 128 + (127 - y)) * 4;
		const u8 *pe = pal + 4 * (uint)px[i];
		o[0] = pe[0];
		o[1] = pe[1];
		o[2] = pe[2];
		o[3] = pe[3] >= 0x80 ? 255 : (u8)(pe[3] * 2);
	}
	return rgba;
}

// SEGA "AFCE" face sets (Sakura Wars "MARS/GR/dummy/MRS_KAO_DUMMY.GRD"): {"AFCE", u32 size without header, u32 header size 0x10, 0}, body {u32 count (4), u32 table offset 0x20, 0, 0} and at 0x20 a table of
// u16 pairs (0x20 / 0x30 bytes in the sample: 2, 2, 1, 1, 1, 1 ... the meaning is not decoded), followed by "HTEX" chunks (stride size + 0x10, flags 0x400 = Melt packed, see sega_melt.inc) that hold the face textures
// as "HTSF" containers. Written as "<name>.json" (header words and table), "<name>.<n>.htsf" and, for the 128 x 128 palette textures, "<name>.<n>.png".
static enumError extract_sega_afce_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".GRD") && !is_ext (arg, ".grd"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x40 || memcmp (c, "AFCE", 4) || rd_le32 (c + 8) != 0x10 || rd_le32 (c + 0x14) != 0x20)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	// the HTEX chunks start at the first "HTEX" tag after the table
	size_t p = 0x20;
	while (p + 0x10 <= cs && memcmp (c + p, "HTEX", 4))
		p += 4;
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "{\n\t\"format\": \"Sakura Wars AFCE\",\n\t\"count\": %u,\n\t\"table\": [", rd_le32 (c + 0x10));
	for (size_t i = 0x20; i + 4 <= p; i += 4)
		sumo_printf (&b, "%s%u", i > 0x20 ? ", " : "", rd_le32 (c + i));
	sumo_printf (&b, "],\n\t\"textures\": [");
	char dest[PATH_MAX], name[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	const char *sl = strrchr (arg, '/');
	snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
	char *dot = strrchr (name, '.');
	if (dot)
		*dot = 0;
	u8 *dec = MALLOC (0x1000000);
	enumError err = ERR_OK;
	uint nt = 0;
	while (p + 0x20 <= cs && !memcmp (c + p, "HTEX", 4) && !err)
	{
		const u32 hsz = rd_le32 (c + p + 4);
		size_t n = 0;
		const bool ok = (rd_le32 (c + p + 0xc) & 0x400) && p + 0x10 + (size_t)hsz <= cs && sega_melt (c + p, (size_t)hsz + 0x10, 0x20, dec, 0x1000000, &n) && n > 0x30 && !memcmp (dec, "HTSF", 4);
		sumo_printf (&b, "%s{\"htsf_bytes\": %zu}", nt ? ", " : "", ok ? n : 0);
		if (ok && !testmode)
		{
			char fn[PATH_MAX];
			snprintf (fn, sizeof (fn), "%s.%u.htsf", name, nt);
			if (verbose >= 0)
				fprintf (stdlog, "EXTRACT SEGA-AFCE:%s (HTSF %zu bytes) -> %s/%s\n", arg, n, dest, fn);
			err = shade_write (arg, dest, basedir, fn, dec, (uint)n);
			u8 *rgba = err ? 0 : sega_face_rgba (dec, n);
			if (rgba)
			{
				char png[PATH_MAX], ext[32];
				snprintf (ext, sizeof (ext), ".%u.png", nt);
				beside_source_dest_ext (png, sizeof (png), arg, ext);
				if (verbose >= 0)
					fprintf (stdlog, "EXTRACT SEGA-AFCE:%s (128x128 C8) -> PNG:%s\n", arg, png);
				const enumError e = SaveDecodedRGBAToPNG (rgba, 128, 128, &le_func, png, 0, opt_overwrite);
				if (e && e != ERR_NOTHING_TO_DO)
					err = e;
			}
		}
		nt++;
		p += 0x10 + (size_t)hsz;
	}
	sumo_printf (&b, "]\n}\n");
	FREE (dec);
	FREE (c);
	if (!err && b.ok && b.n && b.n < 0xffffffffu)
	{
		char jn[PATH_MAX];
		snprintf (jn, sizeof (jn), "%s.json", name);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SEGA-AFCE:%s (%u textures) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nt, dest, jn);
		err = shade_write (arg, dest, basedir, jn, (u8 *)b.p, (uint)b.n);
	}
	else if (!err)
		err = ERR_NOTHING_TO_DO;
	FREE (b.p);
	return err;
}
