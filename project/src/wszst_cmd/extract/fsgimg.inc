// FreeStyleGames "FSG-FILE-SYSTEM" images (DJ Hero 2 "DISC0.IMG" / "DISC0.IMG.part0".."partN", the parts are concatenated). Layout after the 16 byte magic (big endian): u32 2, u32 header length, u32 sector count, u32 sector map offset, u32 data base offset (0x20000), u32 ?, u32 8, u32 file count, u32 0, u32 checksum, then file count nodes {u32 path hash, u8 type, u24 record offset}. A record is {u32 data offset >> 10, u32 size} (data at base + (value << 10)). Directory data is a list of NUL terminated names ("D<name>" or "F<name>", an empty name ends it); node lookups use a FNV-like hash of the upper case path without leading slash (hash = 1677619 * hash ^ byte, start 2166136261). Format notes follow the public GameArchives (maxton) FSGIMG reader.
typedef struct fsg_ctx_t
{
	FILE *f[64];
	u64 start[65]; // cumulative start offsets, start[n] = total size
	uint n;
	u32 base;
	struct fsg_node_t
	{
		u32 hash;
		u32 rec;
	} *nodes;
	uint nnodes;
	ccp dest;
	ccp arg;
	enumError err;
	uint written, skipped;
} fsg_ctx_t;

static bool fsg_read (fsg_ctx_t *c, u64 pos, void *buf, size_t n)
{
	u8 *p = buf;
	while (n)
	{
		uint i = 0;
		while (i < c->n && pos >= c->start[i + 1])
			i++;
		if (i >= c->n)
			return false;
		u64 avail = c->start[i + 1] - pos;
		size_t k = n < avail ? n : (size_t)avail;
		if (fseeko (c->f[i], pos - c->start[i], SEEK_SET) || fread (p, 1, k, c->f[i]) != k)
			return false;
		p += k;
		pos += k;
		n -= k;
	}
	return true;
}

static u32 fsg_hash (ccp s)
{
	if (*s == '/')
		s++;
	u32 h = 2166136261U;
	for (; *s; s++)
		h = (1677619U * h) ^ (u8)toupper ((u8)*s);
	return h;
}

static int fsg_node_cmp (const void *a, const void *b)
{
	const u32 x = ((const struct fsg_node_t *)a)->hash, y = ((const struct fsg_node_t *)b)->hash;
	return x < y ? -1 : x > y;
}

static bool fsg_lookup (fsg_ctx_t *c, ccp path, u64 *data, u32 *size)
{
	struct fsg_node_t key = { fsg_hash (path), 0 };
	struct fsg_node_t *r = bsearch (&key, c->nodes, c->nnodes, sizeof (key), fsg_node_cmp);
	if (!r)
		return false;
	u8 rec[8];
	if (!fsg_read (c, r->rec, rec, 8))
		return false;
	*data = ((u64)be32 (rec) << 10) + c->base;
	*size = be32 (rec + 4);
	return true;
}

static void fsg_walk (fsg_ctx_t *c, u64 listpos, ccp rel, uint depth)
{
	if (depth > 24 || c->err)
		return;
	// read the whole name list first (names end with an empty string, at most 1 MiB)
	enum { MAXLIST = 1 << 20 };
	char *list = MALLOC (MAXLIST);
	size_t len = 0;
	while (len < MAXLIST - 1)
	{
		const size_t k = MAXLIST - 1 - len < 4096 ? MAXLIST - 1 - len : 4096;
		size_t got = k;
		while (got && !fsg_read (c, listpos + len, list + len, got))
			got /= 2;
		if (!got)
			break;
		bool end = false;
		for (size_t i = len; i + 1 < len + got; i++)
			if (!list[i] && !list[i + 1])
				end = true;
		len += got;
		if (end)
			break;
	}
	list[len] = 0;
	for (size_t i = 0; i < len && list[i] && !c->err;)
	{
		ccp nm = list + i;
		i += strlen (nm) + 1;
		if ((nm[0] != 'D' && nm[0] != 'F') || !nm[1] || strchr (nm + 1, '/') || strchr (nm + 1, '\\') || !strcmp (nm + 1, "..") || !strcmp (nm + 1, "."))
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s%s%s", rel, *rel ? "/" : "", nm + 1);
		u64 data;
		u32 size;
		if (!fsg_lookup (c, path, &data, &size))
		{
			c->skipped++;
			continue;
		}
		if (nm[0] == 'D')
			fsg_walk (c, data, path, depth + 1);
		else if (data + size > c->start[c->n])
			c->skipped++; // lives in a part that is not present
		else if (!testmode)
		{
			char out[PATH_MAX];
			snprintf (out, sizeof (out), "%s/%s", c->dest, path);
			File_t F;
			c->err = CreateFileOpt (&F, true, out, false, c->arg);
			if (F.f)
			{
				enum { BS = 1 << 20 };
				u8 *buf = MALLOC (BS);
				for (u64 done = 0; done < size && !c->err;)
				{
					const size_t k = size - done < BS ? (size_t)(size - done) : BS;
					if (!fsg_read (c, data + done, buf, k) || fwrite (buf, 1, k, F.f) != k)
						c->err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Copying %zu bytes failed: %s\n", k, out);
					done += k;
				}
				FREE (buf);
			}
			ResetFile (&F, opt_preserve);
			c->written++;
		}
		else
			c->written++;
	}
	FREE (list);
}

static enumError extract_fsgimg_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	FILE *f0 = fopen (arg, "rb");
	if (!f0)
		return ERR_NOTHING_TO_DO;
	u8 hd[0x38];
	if (fread (hd, 1, sizeof (hd), f0) != sizeof (hd) || memcmp (hd, "FSG-FILE-SYSTEM", 16) || be32 (hd + 0x10) != 2)
	{
		fclose (f0);
		return ERR_NOTHING_TO_DO;
	}
	fsg_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.f[0] = f0;
	c.n = 1;
	// further parts: "<base>.part0" -> "<base>.part1", ...
	const size_t al = strlen (arg);
	if (al > 6 && !strncmp (arg + al - 6, ".part", 5) && isdigit ((u8)arg[al - 1]))
	{
		char name[PATH_MAX];
		for (uint i = arg[al - 1] - '0' + 1; i < 10 && c.n < 64; i++)
		{
			snprintf (name, sizeof (name), "%.*s%u", (int)al - 1, arg, i);
			c.f[c.n] = fopen (name, "rb");
			if (!c.f[c.n])
				break;
			c.n++;
		}
	}
	for (uint i = 0; i < c.n; i++)
	{
		fseeko (c.f[i], 0, SEEK_END);
		c.start[i + 1] = c.start[i] + (u64)ftello (c.f[i]);
	}
	const u32 nfiles = be32 (hd + 0x2c);
	c.base = be32 (hd + 0x20);
	enumError err = ERR_NOTHING_TO_DO;
	if (nfiles && nfiles < 1000000 && c.base && c.base < c.start[1] && 0x38 + 8 * (u64)nfiles <= c.start[1])
	{
		c.nodes = MALLOC (sizeof (*c.nodes) * nfiles);
		u8 *t = MALLOC (8 * (size_t)nfiles);
		if (fsg_read (&c, 0x38, t, 8 * (size_t)nfiles))
		{
			for (uint i = 0; i < nfiles; i++)
			{
				c.nodes[i].hash = be32 (t + 8 * i);
				c.nodes[i].rec = be32 (t + 8 * i + 4) & 0xffffff;
			}
			c.nnodes = nfiles;
			qsort (c.nodes, nfiles, sizeof (*c.nodes), fsg_node_cmp);
			char dest[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			c.dest = dest;
			c.arg = arg;
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT FSGIMG:%s (%u parts) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, c.n, dest);
			fsg_walk (&c, c.base, "", 0);
			err = c.err ? c.err : ERR_OK;
			if (c.skipped && verbose >= 0)
				fprintf (stdlog, "FSGIMG: %u entries skipped (missing parts or unknown nodes)\n", c.skipped);
		}
		FREE (t);
		FREE (c.nodes);
	}
	for (uint i = 0; i < c.n; i++)
		fclose (c.f[i]);
	return err;
}
