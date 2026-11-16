// IO Interactive Glacier localisation trees (Mini Ninjas "<scene><Language>.LOC" inside the scene
// ".zip" packs). The file is one group without name: {u8 count, (count - 1) x u32 LE offset of
// child i relative to child 0}, child 0 follows directly. Every child is {NUL-terminated name, u8
// type}: 0x10 = group (count + offsets as above, count 0 = empty), 0x09 = string (UTF-8 text, NUL,
// u32 0), 0x29 = string with an extra u32 (NUL, u32 id, u32 id; dialogue lines, ids step by 0xe0),
// 0x08 = name only. The walk must end exactly at EOF and every stored offset must match (all 138
// retail files: 96,586 strings). Written as
// "<file>.d/strings.tsv" ("path<TAB>text[<TAB>id]", \n \r \t \\ escaped).
typedef struct gloc_t
{
	const u8 *b;
	size_t n;
	njm_out_t o;
	uint strings;
	char path[1024];
} gloc_t;

static size_t gloc_group (gloc_t *g, size_t o, uint depth);

static size_t gloc_node (gloc_t *g, size_t o, uint depth)
{
	const u8 *e = memchr (g->b + o, 0, g->n - o);
	if (!e || (size_t)(e - g->b) + 2 > g->n)
		return 0;
	const size_t nl = (size_t)(e - (g->b + o)), pl = strlen (g->path);
	if (pl + 1 + nl >= sizeof (g->path))
		return 0;
	const uint type = e[1];
	size_t p = (size_t)(e - g->b) + 2, end = 0;
	g->path[pl] = '/';
	memcpy (g->path + pl + 1, g->b + o, nl);
	g->path[pl + 1 + nl] = 0;
	if (type == 0x10)
		end = gloc_group (g, p, depth + 1);
	else if (type == 0x08)
		end = p;
	else if (type == 0x09 || type == 0x29)
	{
		const u8 *t = memchr (g->b + p, 0, g->n - p);
		const size_t tail = type == 0x29 ? 8 : 4;
		if (t && (size_t)(t - g->b) + 1 + tail <= g->n)
		{
			njm_put (&g->o, "%s\t", g->path + 1);
			for (const u8 *c = g->b + p; c < t; c++)
				if (*c == '\n')
					njm_put (&g->o, "\\n");
				else if (*c == '\r')
					njm_put (&g->o, "\\r");
				else if (*c == '\t')
					njm_put (&g->o, "\\t");
				else if (*c == '\\')
					njm_put (&g->o, "\\\\");
				else
					njm_put (&g->o, "%c", *c);
			if (type == 0x29)
				njm_put (&g->o, "\t%u", rd_be32 (t + 1));
			njm_put (&g->o, "\n");
			g->strings++;
			end = (size_t)(t - g->b) + 1 + tail;
		}
	}
	g->path[pl] = 0;
	return end;
}

static size_t gloc_group (gloc_t *g, size_t o, uint depth)
{
	if (depth > 64 || o >= g->n)
		return 0;
	const uint cnt = g->b[o];
	if (!cnt)
		return o + 1;
	const size_t base = o + 1 + 4 * (size_t)(cnt - 1);
	if (base > g->n)
		return 0;
	size_t p = base;
	for (uint i = 0; i < cnt; i++)
	{
		if (i && base + rd_le32 (g->b + o + 1 + 4 * (i - 1)) != p)
			return 0;
		if (!(p = gloc_node (g, p, depth)))
			return 0;
	}
	return p;
}

static enumError extract_glacier_loc_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".loc"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 8 || sz > 0x4000000
		|| !raw[0])
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	gloc_t *g = CALLOC (1, sizeof (*g));
	if (!g)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	g->b = raw;
	g->n = sz;
	g->o.cap = 1 << 16;
	g->o.b = MALLOC (g->o.cap);
	enumError err = ERR_NOTHING_TO_DO;
	if (g->o.b && gloc_group (g, 0, 0) == sz && g->strings)
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT GLACIER-LOC:%s (%u strings) -> %s/strings.tsv\n",
				verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, g->strings, dest);
		err = testmode
			? ERR_OK
			: shade_write (arg, dest, basedir, "strings.tsv", (u8 *)g->o.b, (uint)g->o.used);
	}
	FREE (g->o.b);
	FREE (g);
	FREE (raw);
	return err;
}
