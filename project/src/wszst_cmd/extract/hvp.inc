// Zoe Mode / Ravenscourt ".hvp" file packs (Aladdin Magic Racer, X Factor,
// Knockout Party, Obscure: The Aftermath; all big-endian):
//   u32 version (0x50000, or 0x40000 without a name pool), u32 0, u32 count,
//   u32 table_checksum, [v5: u32 pool_size, char pool[pool_size]],
//   count * 28-byte records {u32 name_hash, u32 flags, u32 crc, u32 size,
//                            u32 name_offset, u32 a, u32 b}.
// flags & 4 marks a directory (a = child count, b = first child record);
// otherwise a is the absolute data offset and b the stored size. flags & 1
// means the member is an LZO1X stream. Record 0 is the root.
#define HVP_MAX_DEPTH 32

typedef struct hvp_ctx_t
{
	const u8 *raw;
	size_t raw_size;
	const u8 *tab;
	const u8 *pool;
	uint pool_size, count, n_files;
	ccp arg, dest, basedir;
	enumError err;
} hvp_ctx_t;

static void hvp_walk (hvp_ctx_t *c, uint idx, const char *path, uint depth)
{
	if (c->err || idx >= c->count || depth > HVP_MAX_DEPTH)
		return;
	const u8 *r = c->tab + 28ull * idx;
	const uint flags = rd_be32 (r + 4), nameoff = rd_be32 (r + 16);
	const uint a = rd_be32 (r + 20), b = rd_be32 (r + 24);

	char name[256];
	if (c->pool && nameoff < c->pool_size && memchr (c->pool + nameoff, 0, c->pool_size - nameoff))
		snprintf (name, sizeof (name), "%s", (ccp)c->pool + nameoff);
	else
		snprintf (name, sizeof (name), "%08x_%u", rd_be32 (r), idx);
	if (idx && !OwnedNameOk (name))
		snprintf (name, sizeof (name), "%08x_%u", rd_be32 (r), idx);

	char full[PATH_MAX];
	if (idx)
		snprintf (full, sizeof (full), "%s%s%s", path, *path ? "/" : "", name);
	else
		*full = 0;

	if (flags & 4)
	{
		if (a > c->count || b > c->count || a > c->count - b)
			return;
		for (uint i = 0; i < a && !c->err; i++)
			hvp_walk (c, b + i, full, depth + 1);
		return;
	}
	const u32 size = rd_be32 (r + 12);
	if (!idx || (u64)a + b > c->raw_size)
		return;

	const u8 *data = c->raw + a;
	u8 *dec = 0;
	uint dsz = b;
	if (flags & 1)
	{
		if (DecodeLZO1XGrow (&dec, &dsz, data, b))
			return;
		data = dec;
	}
	(void)size;

	char out[PATH_MAX];
	snprintf (out, sizeof (out), "%s/%s%s", c->dest, c->basedir ? c->basedir : "", full);
	if (!testmode)
	{
		File_t F;
		c->err = CreateFileOpt (&F, true, out, false, c->arg);
		if (F.f && dsz && fwrite (data, 1, dsz, F.f) != dsz)
			c->err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", dsz, out);
		ResetFile (&F, opt_preserve);
	}
	c->n_files++;
	FREE (dec);
}

static enumError extract_hvp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".hvp"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint ver = raw_size >= 16 ? rd_be32 (raw) : 0;
	if ((ver != 0x50000 && ver != 0x40000) || raw_size > UINT_MAX)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	hvp_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.raw = raw;
	c.raw_size = raw_size;
	c.count = rd_be32 (raw + 8);
	size_t p = 16;
	if (ver == 0x50000)
	{
		if (raw_size < 20)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		c.pool_size = rd_be32 (raw + 16);
		c.pool = raw + 20;
		p = 20 + (size_t)c.pool_size;
	}
	if (!c.count || p > raw_size || (raw_size - p) / 28 < c.count)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	c.tab = raw + p;
	// The pool must end in a NUL so name lookups cannot run off its end.
	if (c.pool_size && c.pool[c.pool_size - 1])
		c.pool = 0;

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	c.arg = arg;
	c.dest = dest;
	c.basedir = basedir;
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT HVP:%s (%u records) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, c.count, dest);
	hvp_walk (&c, 0, "", 0);
	FREE (raw);
	enumError err = c.err;
	if (!err && !c.n_files)
		return ERR_NOTHING_TO_DO;
	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}
