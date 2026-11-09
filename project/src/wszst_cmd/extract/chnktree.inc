// "CHNK" tree files (Tierpension / Paws & Claws / Gesundheitscoach engine: ".scene", ".object", ".phn", ".var", ".cha", ...,
// big endian): the file starts "CHNK" followed by one root chunk, every chunk = {char tag[4], u32 size = byte length of the
// whole chunk including this 8 byte header, payload}. A chunk is a container when its payload (optionally after one u32
// count / flag word) is an exact sequence of valid chunks (optionally followed by a 4 byte terminator equal to the parent tag). Written as "tree.json": nested objects {tag, offset, size,
// head (the word before the children), children} for containers and {tag, offset, size, hex} for leaves (payload bytes).
typedef struct chnk_ctx_t { const u8 *d; size_t sz; njm_out_t o; uint nodes; } chnk_ctx_t;

static bool chnk_valid_tag (const u8 *p)
{
	for (uint i = 0; i < 4; i++)
		if (!((p[i] >= 'A' && p[i] <= 'Z') || (p[i] >= '0' && p[i] <= '9') || p[i] == '_'))
			return false;
	return true;
}

// do the bytes [p, end) form an exact sequence of valid chunks?
static bool chnk_tiles (const chnk_ctx_t *c, size_t p, size_t end, const u8 *parent)
{
	if (p >= end)
		return false;
	while (p < end)
	{
		if (end - p == 4 && !memcmp (c->d + p, parent, 4))
			return true; // 4 byte terminator (the parent tag) after the last chunk
		if (p + 8 > end || !chnk_valid_tag (c->d + p))
			return false;
		const u32 s = rd_be32 (c->d + p + 4);
		if (s < 8 || s > end - p)
			return false;
		p += s;
	}
	return p == end;
}

static void chnk_emit (chnk_ctx_t *c, size_t p, size_t end, uint depth)
{
	bool first = true;
	while (p < end && c->nodes < 2000000)
	{
		if (end - p == 4)
			break; // terminator
		const u32 s = rd_be32 (c->d + p + 4);
		c->nodes++;
		njm_put (&c->o, "%s\n%*s{\"tag\": \"%.4s\", \"offset\": %zu, \"size\": %u", first ? "" : ",", (int)(depth * 1 + 1), "", c->d + p, p, s);
		first = false;
		const size_t ps = p + 8, pe = p + s;
		size_t cs = 0;
		if (depth < 40 && chnk_tiles (c, ps, pe, c->d + p))
			cs = ps;
		else if (depth < 40 && pe >= ps + 4 && chnk_tiles (c, ps + 4, pe, c->d + p))
			cs = ps + 4;
		if (cs)
		{
			if (cs != ps)
				njm_put (&c->o, ", \"head\": %u", rd_be32 (c->d + ps));
			njm_put (&c->o, ", \"children\": [");
			chnk_emit (c, cs, pe, depth + 1);
			njm_put (&c->o, "]}");
		}
		else
		{
			njm_put (&c->o, ", \"hex\": \"");
			for (size_t k = ps; k < pe; k++)
				njm_put (&c->o, "%02x", c->d[k]);
			njm_put (&c->o, "\"}");
		}
		p = pe;
	}
}

static enumError extract_chnktree_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 hd[12];
	const size_t got = fread (hd, 1, 12, f);
	fclose (f);
	if (got != 12 || memcmp (hd, "CHNKCHNK", 8))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 16 || sz > 0x10000000 || rd_be32 (raw + 8) != sz - 4)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	chnk_ctx_t c = { raw, sz, { MALLOC (1 << 16), 0, 1 << 16 }, 0 };
	if (!c.o.b)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	njm_put (&c.o, "{\"root\": [");
	chnk_emit (&c, 4, sz, 0);
	njm_put (&c.o, "\n]}\n");
	enumError err = ERR_OK;
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT CHNK-TREE:%s (%u chunks) -> %s/tree.json\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg,
			c.nodes, dest);
	if (!testmode)
		err = c.o.b ? shade_write (arg, dest, basedir, "tree.json", (u8 *)c.o.b, (uint)c.o.used) : ERR_OUT_OF_MEMORY;
	FREE (c.o.b);
	FREE (raw);
	return err;
}
