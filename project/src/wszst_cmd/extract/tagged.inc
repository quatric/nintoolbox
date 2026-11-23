// Tagged chunk files of one DS engine (about 40 games, e.g. "Successfully Learning English", "3D Twist Match"), little endian: {char magic[4], u32 size} followed by chunks {char tag[4], u32 size, data} where the size excludes the
// 8 byte header and the root size is the file size minus 8. "NIMG" (".img"): "IHDR" {u32 format 1, u32 width, u32 height} and "CPXL" holding one Nintendo LZ11 stream that unpacks to width * height * 4 bytes of RGBA pixels,
// written as "<name>.png" (other pixel formats are left alone). "ANIM" (".ani", containers FRAM / FLST) and "NFID" (".fnt") are dumped as a JSON chunk tree "<name>.json": leaf chunks as u32 words (plus the text when they
// carry a string); what the fields mean (sprite frames, glyph table) is not decoded.
static void tagged_json (sumo_buf_t *b, const u8 *d, size_t n, uint level)
{
	size_t p = 0;
	bool first = true;
	while (p + 8 <= n)
	{
		const size_t sz = rd_le32 (d + p + 4);
		if (sz > n - p - 8)
			break;
		const u8 *c = d + p + 8;
		char tag[5];
		for (uint i = 0; i < 4; i++)
			tag[i] = d[p + i] >= 0x20 && d[p + i] < 0x7f && d[p + i] != '"' && d[p + i] != '\\' ? (char)d[p + i] : '_';
		tag[4] = 0;
		sumo_printf (b, "%s\n%*s{\"tag\": \"%s\", \"size\": %zu", first ? "" : ",", (int)(level + 1) * 2, "", tag, sz);
		first = false;
		if (!memcmp (tag, "FRAM", 4) || !memcmp (tag, "FLST", 4))
		{
			sumo_printf (b, ", \"chunks\": [");
			tagged_json (b, c, sz, level + 1);
			sumo_printf (b, "\n%*s]", (int)(level + 1) * 2, "");
		}
		else
		{
			sumo_printf (b, ", \"u32\": [");
			for (size_t i = 0; i + 4 <= sz && i < 4 * 4096; i += 4)
				sumo_printf (b, "%s%u", i ? "," : "", rd_le32 (c + i));
			sumo_printf (b, "]");
			size_t best0 = 0, best1 = 0;
			for (size_t i = 0; i < sz;)
			{
				size_t j = i;
				while (j < sz && c[j] >= 0x20 && c[j] < 0x7f && c[j] != '"' && c[j] != '\\')
					j++;
				if (j - i > best1 - best0)
					best0 = i, best1 = j;
				i = j + 1;
			}
			if (best1 - best0 >= 3 && best1 - best0 < 512)
				sumo_printf (b, ", \"text\": \"%.*s\"", (int)(best1 - best0), (ccp)c + best0);
		}
		sumo_printf (b, "}");
		p += 8 + sz;
	}
}

static enumError extract_tagged_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0, *pix = 0;
	size_t sz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	sumo_buf_t b = { 0, 0, 0, true };
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	const bool nimg = sz > 16 && !memcmp (raw, "NIMG", 4), anim = sz > 16 && (!memcmp (raw, "ANIM", 4) || !memcmp (raw, "NFID", 4));
	if ((!nimg && !anim) || rd_le32 (raw + 4) != sz - 8)
		goto done;
	const char *sl = strrchr (arg, '/');
	char name[PATH_MAX];
	snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
	char *dot = strrchr (name, '.');
	if (dot)
		*dot = 0;
	if (nimg)
	{
		uint w = 0, h = 0, fmt = 0, pn = 0;
		const u8 *cp = 0;
		for (size_t p = 8; p + 8 <= sz;)
		{
			const size_t cs = rd_le32 (raw + p + 4);
			if (cs > sz - p - 8)
				break;
			if (!memcmp (raw + p, "IHDR", 4) && cs >= 12)
				fmt = rd_le32 (raw + p + 8), w = rd_le32 (raw + p + 12), h = rd_le32 (raw + p + 16);
			else if (!memcmp (raw + p, "CPXL", 4))
				cp = raw + p + 8, pn = (uint)cs;
			p += 8 + cs;
		}
		uint outn;
		if (fmt != 1 || !w || !h || w > 4096 || h > 4096 || !cp || DecodeLZ10LZ11 (&pix, &outn, cp, pn) || outn != w * h * 4)
			goto done;
		err = kyl_write_rgba_png (arg, basedir, name, (uint)-1, w, h, pix);
		goto done;
	}
	sumo_printf (&b, "{\n\t\"format\": \"%.4s\",\n\t\"chunks\": [", (ccp)raw);
	tagged_json (&b, raw + 8, sz - 8, 0);
	sumo_printf (&b, "\n\t]\n}\n");
	if (b.ok && b.n && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		strncat (name, ".json", sizeof (name) - strlen (name) - 1);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT %.4s:%s -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", (ccp)raw, arg, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
done:
	FREE (b.p);
	FREE (pix);
	FREE (raw);
	return err;
}
