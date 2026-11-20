// Sonic Team text tables (Sonic Colors, Wii: ".xtb", the in game messages in every language; 193 files of the retail disc, little endian). Layout: 8 bytes 0xff, u16 version (2), u16 style count N, 6 bytes, then N
// styles {u8 name length, name ("s51", "Default"), 10 bytes {1, font size, 0, 0, 0, r, g, b, 0, 0}}, u32 group count G, u32 0, then G groups {u8 name length, name ("NAND"), u8 message count} each with
// that many messages {u8 id length, id ("INSSPACE"), u8 style length, style name, u32 text bytes, u32 text characters (bytes / 2), UTF-16LE text with "\n" line breaks}. The file ends with the last text.
// Written as "<file>.json" (styles, groups, messages with their text) and "<file>.txt" (one "group.id<TAB>text" line per message, line breaks written as \n).
static void xtb_json_text (sumo_buf_t *b, const u8 *p, size_t units)
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

static enumError extract_sonic_xtb_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".xtb"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x20)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	static const u8 ff[8] = { 255, 255, 255, 255, 255, 255, 255, 255 };
	if (memcmp (c, ff, 8) || rd_le16 (c + 8) != 2)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	const uint ns = rd_le16 (c + 10);
	size_t p = 0x12;
	bool ok = true;
	sumo_buf_t b = { 0, 0, 0, true }, t = { 0, 0, 0, true };
	sumo_printf (&b, "{\n\t\"format\": \"Sonic Team XTB\",\n\t\"styles\": [");
	for (uint i = 0; ok && i < ns; i++)
	{
		ok = p < cs;
		const size_t nl = ok ? c[p] : 0;
		ok = ok && p + 1 + nl + 10 <= cs;
		if (!ok)
			break;
		sumo_printf (&b, "%s\n\t\t{\"name\": \"%.*s\", \"size\": %u, \"color\": \"%02x%02x%02x\", \"raw\": [%u, %u, %u, %u, %u, %u, %u, %u, %u, %u]}", i ? "," : "", (int)nl, (ccp)c + p + 1, c[p + 2 + nl],
			c[p + 6 + nl], c[p + 7 + nl], c[p + 8 + nl], c[p + 1 + nl], c[p + 2 + nl], c[p + 3 + nl], c[p + 4 + nl], c[p + 5 + nl], c[p + 6 + nl], c[p + 7 + nl], c[p + 8 + nl], c[p + 9 + nl],
			c[p + 10 + nl]);
		p += 1 + nl + 10;
	}
	sumo_printf (&b, "\n\t],\n\t\"groups\": [");
	const uint ng = ok && p + 8 <= cs ? rd_le32 (c + p) : 0;
	ok = ok && p + 8 <= cs && ng <= 4096;
	p += 8;
	uint nm = 0;
	for (uint g = 0; ok && g < ng; g++)
	{
		ok = p < cs;
		const size_t gl = ok ? c[p] : 0;
		ok = ok && p + 2 + gl <= cs;
		if (!ok)
			break;
		const uint cnt = c[p + 1 + gl];
		char gname[256];
		snprintf (gname, sizeof (gname), "%.*s", (int)gl, (ccp)c + p + 1);
		sumo_printf (&b, "%s\n\t\t{\"name\": \"%s\", \"messages\": [", g ? "," : "", gname);
		p += 2 + gl;
		for (uint m = 0; ok && m < cnt; m++)
		{
			ok = p < cs;
			const size_t il = ok ? c[p] : 0;
			ok = ok && p + 2 + il <= cs;
			if (!ok)
				break;
			const size_t sl = c[p + 1 + il];
			ok = p + 2 + il + sl + 8 <= cs;
			if (!ok)
				break;
			const size_t q = p + 2 + il + sl;
			const u32 bl = rd_le32 (c + q), cl = rd_le32 (c + q + 4);
			ok = bl == 2ull * cl && q + 8 + bl <= cs;
			if (!ok)
				break;
			sumo_printf (&b, "%s\n\t\t\t{\"id\": \"%.*s\", \"style\": \"%.*s\", \"text\": \"", m ? "," : "", (int)il, (ccp)c + p + 1, (int)sl, (ccp)c + p + 2 + il);
			xtb_json_text (&b, c + q + 8, cl);
			sumo_printf (&b, "\"}");
			sumo_printf (&t, "%s.%.*s\t", gname, (int)il, (ccp)c + p + 1);
			for (u32 k = 0; k < cl; k++)
			{
				const uint u = rd_le16 (c + q + 8 + 2 * k);
				if (u == '\n')
					sumo_printf (&t, "\\n");
				else if (u < 0x80)
					sumo_printf (&t, "%c", (int)u);
				else if (u < 0x800)
					sumo_printf (&t, "%c%c", (int)(0xc0 | (u >> 6)), (int)(0x80 | (u & 0x3f)));
				else
					sumo_printf (&t, "%c%c%c", (int)(0xe0 | (u >> 12)), (int)(0x80 | ((u >> 6) & 0x3f)), (int)(0x80 | (u & 0x3f)));
			}
			sumo_printf (&t, "\n");
			p = q + 8 + bl;
			nm++;
		}
		sumo_printf (&b, "\n\t\t]}");
	}
	sumo_printf (&b, "\n\t]\n}\n");
	ok = ok && p == cs;
	FREE (c);
	enumError err = ERR_NOTHING_TO_DO;
	if (ok && b.ok && t.ok && b.n && b.n < 0xffffffffu && t.n < 0xffffffffu)
	{
		char dest[PATH_MAX], name[PATH_MAX], name2[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			*dot = 0;
		snprintf (name2, sizeof (name2), "%s.txt", name);
		strncat (name, ".json", sizeof (name) - strlen (name) - 1);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SONIC-XTB:%s (%u messages) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nm, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
		if (!err && !testmode)
			err = shade_write (arg, dest, basedir, name2, (u8 *)t.p, (uint)t.n);
	}
	FREE (b.p);
	FREE (t.p);
	return err;
}
