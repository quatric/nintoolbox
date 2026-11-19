// SEGA "GHSL" hierarchical data (Sakura Wars: So Long, My Love, many ".bin" event/area tables, little endian), layout after the game's reader (FUN_803ad27c and its token decoders): {"GHSL", u32 size, u32 0x10, u32 flags}
// (non-zero flags, as in IF/ECATCH/gameinfo.bin, mean a packed body that is not handled); at 0x10 the u32 distance D to the string table at 0x10 + D ({u32 count, count x u32 offset from the table start}, Shift-JIS C strings, written as UTF-8);
// the token stream starts at 0x14. Every token's top two bits select its kind: 00 node {byte: bits 5-4 / 3-2 / 1-0 = byte count - 1 of name index, child offset A, sibling offset B; then name index, A, B as signed LE
// numbers, offsets relative to the token}, 01 attribute {bits 5-4 name index size - 1, bits 3-2 value size - 1, bits 1-0 type (0 number, 2 string index); name index, value}, 10 plain value {bits 3-2 size - 1, bits 1-0
// type; value}, 11 end of the children (bit 0 clear: one more offset field follows). A node's attributes sit between its header and its first child at token + A, children (nodes or plain values) run until the 11 token,
// the next sibling is token + B. Written as JSON {"name", "attrs": {...}, "children": [...]}.
typedef struct sega_ghsl_t
{
	const u8 *d;
	size_t n;
	size_t tab;
	u32 ntab;
	sumo_buf_t *b;
	bool ok;
} sega_ghsl_t;

static s32 ghsl_sgn (const u8 *p, uint len)
{
	u32 v = 0;
	for (uint i = 0; i < len; i++)
		v |= (u32)p[i] << (8 * i);
	if (len < 4 && (v & (1u << (8 * len - 1))))
		v |= ~0u << (8 * len);
	return (s32)v;
}

static void ghsl_str (sega_ghsl_t *g, s32 idx)
{
	sumo_buf_t *b = g->b;
	if (idx < 0 || (u32)idx >= g->ntab)
	{
		g->ok = false;
		sumo_printf (b, "null");
		return;
	}
	const size_t o = g->tab + rd_le32 (g->d + g->tab + 4 + 4 * (size_t)idx);
	if (o >= g->n)
	{
		g->ok = false;
		sumo_printf (b, "null");
		return;
	}
	sumo_printf (b, "\"");
	cucp p = g->d + o;
	while (p < g->d + g->n && *p)
	{
		const u8 c = *p;
		if (c == '"' || c == '\\')
		{
			sumo_printf (b, "\\%c", c);
			p++;
		}
		else if (c < 0x20)
		{
			sumo_printf (b, "\\u%04x", c);
			p++;
		}
		else if (c < 0x80)
		{
			sumo_printf (b, "%c", c);
			p++;
		}
		else
		{
			// Shift-JIS text
			cucp pp = p;
			const int code = ScanShiftJISChar (&pp);
			char u[8];
			if (code > 0 && pp > p)
			{
				*PrintUTF8Char (u, (u32)code) = 0;
				p = pp;
			}
			else
			{
				snprintf (u, sizeof (u), "\\u%04x", c);
				p++;
			}
			sumo_printf (b, "%s", u);
		}
	}
	sumo_printf (b, "\"");
}

static void ghsl_val (sega_ghsl_t *g, uint type, s32 v)
{
	if (type == 2)
		ghsl_str (g, v);
	else
		sumo_printf (g->b, "%d", (int)v);
}

static size_t ghsl_node (sega_ghsl_t *g, size_t t, uint depth)
{
	// returns the offset of the next sibling, 0 on error
	const u8 *d = g->d;
	if (depth > 64 || t + 4 > g->tab)
		return g->ok = false, 0;
	const uint nl = ((d[t] >> 4) & 3) + 1, al = ((d[t] >> 2) & 3) + 1, bl = (d[t] & 3) + 1;
	if (t + 1 + nl + al + bl > g->tab)
		return g->ok = false, 0;
	const s32 name = ghsl_sgn (d + t + 1, nl), a = ghsl_sgn (d + t + 1 + nl, al), bb = ghsl_sgn (d + t + 1 + nl + al, bl);
	if (a <= 0 || bb <= 0 || t + (size_t)a >= g->tab || t + (size_t)bb > g->tab)
		return g->ok = false, 0;
	sumo_buf_t *b = g->b;
	sumo_printf (b, "{\"name\": ");
	ghsl_str (g, name);
	sumo_printf (b, ", \"attrs\": {");
	size_t p = t + 1 + nl + al + bl;
	bool first = true;
	while (p < t + (size_t)a && (d[p] & 0xc0) == 0x40)
	{
		const uint l1 = ((d[p] >> 4) & 3) + 1, l2 = ((d[p] >> 2) & 3) + 1, ty = d[p] & 3;
		if (p + 1 + l1 + l2 > g->tab)
			return g->ok = false, 0;
		sumo_printf (b, "%s", first ? "" : ", ");
		first = false;
		ghsl_str (g, ghsl_sgn (d + p + 1, l1));
		sumo_printf (b, ": ");
		ghsl_val (g, ty, ghsl_sgn (d + p + 1 + l1, l2));
		p += 1 + l1 + l2;
	}
	if (p != t + (size_t)a)
		return g->ok = false, 0;
	sumo_printf (b, "}, \"children\": [");
	size_t q = t + (size_t)a;
	first = true;
	while (g->ok)
	{
		if (q >= g->tab)
			return g->ok = false, 0;
		const uint k = d[q] & 0xc0;
		if (k == 0xc0)
			break;
		sumo_printf (b, "%s", first ? "" : ", ");
		first = false;
		if (k == 0)
		{
			const size_t nx = ghsl_node (g, q, depth + 1);
			if (!nx)
				return 0;
			q = nx;
		}
		else if (k == 0x80)
		{
			const uint l = ((d[q] >> 2) & 3) + 1;
			if (q + 1 + l > g->tab)
				return g->ok = false, 0;
			ghsl_val (g, d[q] & 3, ghsl_sgn (d + q + 1, l));
			q += 1 + l;
		}
		else
			return g->ok = false, 0;
	}
	sumo_printf (b, "]}");
	return t + (size_t)bb;
}

static enumError extract_sega_ghsl_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x20 || memcmp (c, "GHSL", 4) || rd_le32 (c + 8) != 0x10 || rd_le32 (c + 0xc))
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sega_ghsl_t g = { c, cs, 0, 0, &b, true };
	const u64 tab = 0x10 + (u64)rd_le32 (c + 0x10);
	if (tab + 4 > cs)
		g.ok = false;
	else
	{
		g.tab = tab;
		g.ntab = rd_le32 (c + tab);
		if (g.ntab > 0x1000000 || tab + 4 + 4ull * g.ntab > cs)
			g.ok = false;
	}
	if (g.ok)
	{
		sumo_printf (&b, "{\"format\": \"Sakura Wars GHSL\", \"tree\": ");
		ghsl_node (&g, 0x14, 0);
		sumo_printf (&b, "}\n");
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (g.ok && b.ok && b.n && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".json");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SEGA-GHSL:%s -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	FREE (c);
	return err;
}
