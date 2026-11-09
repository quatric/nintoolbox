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

// "name" field of NULO / ENTY records: C string padded with '0' characters to the next 4 byte boundary strictly after the NUL
// (length rounded up to 4 of len + 2). Returns the field length or 0.
static size_t chnk_name (const u8 *p, size_t room, char *out, size_t outsz)
{
	size_t len = 0;
	while (len < room && p[len])
		len++;
	if (len >= room || len == 0 || len + 1 >= outsz)
		return 0;
	const size_t fld = (len + 2 + 3) & ~(size_t)3;
	if (fld > room)
		return 0;
	for (size_t i = 0; i < len; i++)
		if (p[i] < 32 || p[i] > 126 || p[i] == '"' || p[i] == '\\')
			return 0;
	memcpy (out, p, len);
	out[len] = 0;
	return fld;
}

// NULO: u32 version (2), records {name, float m[3][4] (3x3 rotation + translation per row)} ..., tail tag
static bool chnk_nulo (chnk_ctx_t *c, size_t ps, size_t pe)
{
	if (pe - ps < 12 || rd_be32 (c->d + ps) != 2 || memcmp (c->d + pe - 4, "NULO", 4))
		return false;
	size_t q = ps + 4;
	uint n = 0;
	while (q + 4 < pe)
	{
		char nm[64];
		const size_t f = chnk_name (c->d + q, pe - 4 - q, nm, sizeof (nm));
		if (!f || q + f + 48 > pe - 4)
			return false;
		q += f + 48;
		n++;
	}
	if (q != pe - 4)
		return false;
	njm_put (&c->o, ", \"locators\": [");
	q = ps + 4;
	for (uint k = 0; k < n; k++)
	{
		char nm[64];
		const size_t f = chnk_name (c->d + q, pe - 4 - q, nm, sizeof (nm));
		njm_put (&c->o, "%s\n  {\"name\": \"%s\", \"matrix\": [", k ? "," : "", nm);
		for (uint m = 0; m < 12; m++)
			njm_put (&c->o, "%s%.9g", m ? "," : "", njm_f (c->d + q + f + 4 * m));
		njm_put (&c->o, "]}");
		q += f + 48;
	}
	njm_put (&c->o, "]}");
	return true;
}

// ENTY: u32 version (3), u32 kind, u32 1, name, float scale[3], float m[3][4], float box[6] (min, max), then a tail of words and
// padded strings (resource paths and names), tail tag
static bool chnk_enty (chnk_ctx_t *c, size_t ps, size_t pe)
{
	char nm[64];
	if (pe - ps < 12 + 8 || rd_be32 (c->d + ps) != 3 || memcmp (c->d + pe - 4, "ENTY", 4))
		return false;
	const size_t f = chnk_name (c->d + ps + 12, pe - 4 - ps - 12, nm, sizeof (nm));
	size_t q = ps + 12 + f;
	if (!f || q + 84 > pe - 4)
		return false;
	njm_put (&c->o, ", \"entity\": {\"name\": \"%s\", \"kind\": %u, \"flag\": %u, \"scale\": [%.9g,%.9g,%.9g], \"matrix\": [", nm, rd_be32 (c->d + ps + 4),
		rd_be32 (c->d + ps + 8), njm_f (c->d + q), njm_f (c->d + q + 4), njm_f (c->d + q + 8));
	q += 12;
	for (uint m = 0; m < 12; m++)
		njm_put (&c->o, "%s%.9g", m ? "," : "", njm_f (c->d + q + 4 * m));
	q += 48;
	njm_put (&c->o, "], \"box\": [");
	for (uint m = 0; m < 6; m++)
		njm_put (&c->o, "%s%.9g", m ? "," : "", njm_f (c->d + q + 4 * m));
	q += 24;
	njm_put (&c->o, "], \"tail\": [");
	bool first = true;
	while (q + 4 <= pe - 4)
	{
		char s[256];
		size_t sl = 0;
		while (q + sl < pe - 4 && c->d[q + sl] >= 32 && c->d[q + sl] <= 126 && sl < 200)
			sl++;
		size_t fl = 0;
		if (sl >= 3 && q + sl < pe - 4 && !c->d[q + sl])
		{
			memcpy (s, c->d + q, sl);
			s[sl] = 0;
			fl = (sl + 2 + 3) & ~(size_t)3;
			for (char *z = s; *z; z++)
				if (*z == '"' || *z == '\\')
					*z = '?';
		}
		if (fl && q + fl <= pe - 4)
		{
			njm_put (&c->o, "%s\"%s\"", first ? "" : ",", s);
			q += fl;
		}
		else
		{
			njm_put (&c->o, "%s\"%08x\"", first ? "" : ",", rd_be32 (c->d + q));
			q += 4;
		}
		first = false;
	}
	njm_put (&c->o, "]}}");
	return true;
}

// HEAD: u32 version (2), float scale, name (padded like the NULO / ENTY names), then u32 words (the scene header: 3, three counts and
// padding; the fourth word is the NULO locator count in the scenes checked), tail tag
static bool chnk_head (chnk_ctx_t *c, size_t ps, size_t pe)
{
	char nm[64];
	if (pe - ps < 8 + 8 + 4 || rd_be32 (c->d + ps) != 2 || memcmp (c->d + pe - 4, "HEAD", 4))
		return false;
	const size_t f = chnk_name (c->d + ps + 8, pe - 4 - ps - 8, nm, sizeof (nm));
	if (!f || (pe - 4 - (ps + 8 + f)) % 4)
		return false;
	njm_put (&c->o, ", \"scene\": {\"version\": 2, \"scale\": %.9g, \"name\": \"%s\", \"words\": [", njm_f (c->d + ps + 4), nm);
	bool first = true;
	for (size_t q = ps + 8 + f; q + 4 <= pe - 4; q += 4)
	{
		njm_put (&c->o, "%s%u", first ? "" : ",", rd_be32 (c->d + q));
		first = false;
	}
	njm_put (&c->o, "]}}");
	return true;
}

// SPLN: u32 version (2), records {char name[4], padded path string (the ".spline" resource)}, tail tag
static bool chnk_spln (chnk_ctx_t *c, size_t ps, size_t pe)
{
	// version 1 (".spline" files): char name[4], u32 point count, count x float xyz, tail tag
	if (pe - ps >= 12 + 4 && rd_be32 (c->d + ps) == 1 && !memcmp (c->d + pe - 4, "SPLN", 4))
	{
		const u32 cnt = rd_be32 (c->d + ps + 8);
		if (!cnt || (pe - ps - 12 - 4) / 12 != cnt || (pe - ps - 12 - 4) % 12)
			return false;
		for (uint i = 0; i < 4; i++)
			if (c->d[ps + 4 + i] < 32 || c->d[ps + 4 + i] > 126 || c->d[ps + 4 + i] == '"' || c->d[ps + 4 + i] == '\\')
				return false;
		njm_put (&c->o, ", \"spline\": {\"name\": \"%.4s\", \"points\": [", c->d + ps + 4);
		for (u32 k = 0; k < cnt; k++)
		{
			const u8 *r = c->d + ps + 12 + 12 * (size_t)k;
			njm_put (&c->o, "%s[%.9g,%.9g,%.9g]", k ? "," : "", njm_f (r), njm_f (r + 4), njm_f (r + 8));
		}
		njm_put (&c->o, "]}}");
		return true;
	}
	if (pe - ps < 4 + 4 + 4 || rd_be32 (c->d + ps) != 2 || memcmp (c->d + pe - 4, "SPLN", 4))
		return false;
	size_t q = ps + 4;
	uint n = 0;
	while (q + 4 < pe)
	{
		char nm[128];
		for (uint i = 0; i < 4; i++)
			if (q + 4 > pe - 4 || c->d[q + i] < 32 || c->d[q + i] > 126 || c->d[q + i] == '"' || c->d[q + i] == '\\')
				return false;
		const size_t f = chnk_name (c->d + q + 4, pe - 4 - q - 4, nm, sizeof (nm));
		if (!f)
			return false;
		q += 4 + f;
		n++;
	}
	if (q != pe - 4)
		return false;
	njm_put (&c->o, ", \"splines\": [");
	q = ps + 4;
	for (uint k = 0; k < n; k++)
	{
		char nm[128];
		const size_t f = chnk_name (c->d + q + 4, pe - 4 - q - 4, nm, sizeof (nm));
		njm_put (&c->o, "%s\n  {\"name\": \"%.4s\", \"file\": \"%s\"}", k ? "," : "", c->d + q, nm);
		q += 4 + f;
	}
	njm_put (&c->o, "]}");
	return true;
}

// BSTR: u32 version (2), float bounds[6] (min, max), u32 a (node count), b (leaf count), c (index count), d (byte size of the "BSPD"
// chunk below, minus the 8 byte header), then a "BSPD" chunk {u32 3, s32 children[a][2] (>= a: leaf number + a, -1: empty),
// {u32 count, u32 first}[b] leaf ranges into u32 indices[c], tail tag}, tail tag
static bool chnk_bstr (chnk_ctx_t *c, size_t ps, size_t pe)
{
	if (pe - ps < 52 + 8 || rd_be32 (c->d + ps) != 2 || memcmp (c->d + pe - 4, "BSTR", 4) || memcmp (c->d + ps + 44, "BSPD", 4)
		|| memcmp (c->d + pe - 8, "BSPD", 4))
		return false;
	const u32 a = rd_be32 (c->d + ps + 28), b = rd_be32 (c->d + ps + 32), n = rd_be32 (c->d + ps + 36);
	const size_t bs = rd_be32 (c->d + ps + 48);
	if (ps + 44 + bs + 4 != pe || a > 1000000 || b > 1000000 || n > 4000000 || rd_be32 (c->d + ps + 52) != 3
		|| bs != 8 + 4 + 8 * (size_t)a + 8 * (size_t)b + 4 * (size_t)n + 4)
		return false;
	const u8 *q = c->d + ps + 56;
	njm_put (&c->o, ", \"bsp\": {\"bounds\": [");
	for (uint k = 0; k < 6; k++)
		njm_put (&c->o, "%s%.9g", k ? "," : "", njm_f (c->d + ps + 4 + 4 * k));
	njm_put (&c->o, "], \"nodes\": [");
	for (u32 k = 0; k < a; k++)
		njm_put (&c->o, "%s[%d,%d]", k ? "," : "", (int)rd_be32 (q + 8 * (size_t)k), (int)rd_be32 (q + 8 * (size_t)k + 4));
	q += 8 * (size_t)a;
	njm_put (&c->o, "], \"leaves\": [");
	for (u32 k = 0; k < b; k++)
		njm_put (&c->o, "%s[%u,%u]", k ? "," : "", rd_be32 (q + 8 * (size_t)k), rd_be32 (q + 8 * (size_t)k + 4));
	q += 8 * (size_t)b;
	njm_put (&c->o, "], \"indices\": [");
	for (u32 k = 0; k < n; k++)
		njm_put (&c->o, "%s%u", k ? "," : "", rd_be32 (q + 4 * (size_t)k));
	njm_put (&c->o, "]}}");
	return true;
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
		for (uint lead = 0; lead <= 12 && depth < 40 && !cs; lead += 4)
			if (pe >= ps + lead && chnk_tiles (c, ps + lead, pe, c->d + p))
				cs = ps + lead;
		if (cs)
		{
			if (cs != ps)
			{
				njm_put (&c->o, ", \"head\": [");
				for (size_t h = ps; h < cs; h += 4)
					njm_put (&c->o, "%s%u", h > ps ? "," : "", rd_be32 (c->d + h));
				njm_put (&c->o, "]");
			}
			njm_put (&c->o, ", \"children\": [");
			chnk_emit (c, cs, pe, depth + 1);
			njm_put (&c->o, "]}");
		}
		else if (!memcmp (c->d + p, "CAMS", 4) && pe - ps >= 8 + 76 && (pe - ps - 8) % 76 == 0 && !memcmp (c->d + pe - 4, "CAMS", 4))
		{
			// cameras: u32 version (2), then 76 byte records {char name[16], float aspect, float f1, float far, float m[3][4]
			// (rotation rows + translation)}, tail tag
			const uint n = (uint)((pe - ps - 8) / 76);
			njm_put (&c->o, ", \"cameras\": [");
			for (uint k = 0; k < n; k++)
			{
				const u8 *r = c->d + ps + 4 + 76 * (size_t)k;
				char nm[17];
				memcpy (nm, r, 16);
				nm[16] = 0;
				for (char *q = nm; *q; q++)
					if (*q == '"' || *q == '\\' || (u8)*q < 32 || (u8)*q > 126)
						*q = '?';
				njm_put (&c->o, "%s\n  {\"name\": \"%s\", \"params\": [%.9g,%.9g,%.9g], \"matrix\": [", k ? "," : "", nm, njm_f (r + 16), njm_f (r + 20),
					njm_f (r + 24));
				for (uint m = 0; m < 12; m++)
					njm_put (&c->o, "%s%.9g", m ? "," : "", njm_f (r + 28 + 4 * m));
				njm_put (&c->o, "]}");
			}
			njm_put (&c->o, "]}");
		}
		else if (!memcmp (c->d + p, "BSTR", 4) && chnk_bstr (c, ps, pe))
		{
		}
		else if (!memcmp (c->d + p, "SPLN", 4) && chnk_spln (c, ps, pe))
		{
		}
		else if (!memcmp (c->d + p, "HEAD", 4) && chnk_head (c, ps, pe))
		{
		}
		else if (!memcmp (c->d + p, "NULO", 4) && chnk_nulo (c, ps, pe))
		{
		}
		else if (!memcmp (c->d + p, "ENTY", 4) && chnk_enty (c, ps, pe))
		{
		}
		else if (!memcmp (c->d + p, "PHNM", 4) && pe - ps >= 24 && (pe - ps - 8 - 12 - 4) % 16 == 0 && !memcmp (c->d + pe - 4, "PHNM", 4)
			&& rd_be32 (c->d + ps) == 1 && rd_be32 (c->d + ps + 12) == 0x4b)
		{
			// lip-sync phonemes: {u32 1, u32 0}, first entry {end, 0x4b, id} (starts at 0), then {start, end, 0x4b, id}, tail tag
			const u8 *b = c->d + ps + 8;
			const uint n = (uint)((pe - ps - 8 - 12 - 4) / 16);
			njm_put (&c->o, ", \"phonemes\": [[0,%.9g,%u]", njm_f (b), rd_be32 (b + 8));
			for (uint k = 0; k < n; k++)
				njm_put (&c->o, ",[%.9g,%.9g,%u]", njm_f (b + 12 + 16 * k), njm_f (b + 16 + 16 * k), rd_be32 (b + 24 + 16 * k));
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
