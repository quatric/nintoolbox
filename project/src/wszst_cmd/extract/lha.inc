// LHA / LZH archives (".lzh", ".lha"): header levels 0, 1 and 2, methods -lh0- (stored) and -lh5- / -lh6- / -lh7- (LZSS with static Huffman blocks, 8 / 32 / 64 KiB
// window). Names (and directories of level 2 / level 1 extension headers) become paths below "<file>.d/". Other methods (-lh1- .. -lh4-, -lzs-, ...) are skipped.
typedef struct lha_huff_t { uint count[17]; ushort sym[512]; uint n; } lha_huff_t;

typedef struct lha_bits_t
{
	const u8 *p;
	size_t n, pos;
	uint bit; // bits consumed of p[pos]
	bool eof;
} lha_bits_t;

static uint lha_peek1 (lha_bits_t *b)
{
	if (b->pos >= b->n) { b->eof = true; return 0; }
	return b->p[b->pos] >> (7 - b->bit) & 1;
}
static uint lha_get (lha_bits_t *b, uint n)
{
	uint v = 0;
	while (n--)
	{
		v = v << 1 | lha_peek1 (b);
		if (++b->bit == 8) { b->bit = 0; b->pos++; }
	}
	return v;
}

static void lha_build (lha_huff_t *h, const u8 *len, uint n)
{
	memset (h, 0, sizeof (*h));
	h->n = n;
	for (uint i = 0; i < n; i++)
		h->count[len[i]]++;
	h->count[0] = 0;
	uint offs[18] = {0};
	for (uint l = 1; l < 17; l++)
		offs[l + 1] = offs[l] + h->count[l];
	for (uint i = 0; i < n; i++)
		if (len[i])
			h->sym[offs[len[i]]++] = i;
}

// canonical (LHA) decode: codes of one length are consecutive in symbol order
static int lha_decode (lha_bits_t *b, const lha_huff_t *h)
{
	int code = 0, first = 0, index = 0;
	for (uint l = 1; l < 17; l++)
	{
		code |= lha_peek1 (b);
		if (++b->bit == 8) { b->bit = 0; b->pos++; }
		const int cnt = h->count[l];
		if (code - cnt < first)
			return h->sym[index + (code - first)];
		index += cnt;
		first = (first + cnt) << 1;
		code <<= 1;
		if (b->eof)
			return -1;
	}
	return -1;
}

static bool lha_read_pt (lha_bits_t *b, uint nn, uint nbit, int special, lha_huff_t *h, int *constant)
{
	u8 len[512] = {0};
	*constant = -1;
	const uint n = lha_get (b, nbit);
	if (n == 0)
	{
		*constant = lha_get (b, nbit);
		memset (h, 0, sizeof (*h));
		return true;
	}
	if (n > nn)
		return false;
	uint i = 0;
	while (i < n)
	{
		uint c = lha_get (b, 3);
		if (c == 7)
			while (lha_peek1 (b))
			{
				lha_get (b, 1);
				c++;
				if (c > 16) return false;
			}
		// the terminating zero of the unary extension
		if (c >= 7)
			lha_get (b, 1);
		len[i++] = c;
		if ((int)i == special)
		{
			uint z = lha_get (b, 2);
			while (z-- && i < nn)
				len[i++] = 0;
		}
	}
	lha_build (h, len, nn);
	return !b->eof;
}

static u8 *lha_unpack (const u8 *src, size_t n, size_t osize, uint dicbit)
{
	const uint pbit = dicbit == 13 ? 4 : 5, np = dicbit + 1;
	const size_t win = (size_t)1 << dicbit;
	u8 *out = MALLOC (osize ? osize : 1);
	if (!out)
		return 0;
	lha_bits_t b = {src, n, 0, 0, false};
	size_t o = 0;
	while (o < osize && !b.eof)
	{
		const uint blk = lha_get (&b, 16);
		lha_huff_t pt, ct, dt;
		int pc, cc, dc;
		if (!lha_read_pt (&b, 19, 5, 3, &pt, &pc))
			goto fail;
		// character / length code lengths
		u8 clen[510] = {0};
		const uint cn = lha_get (&b, 9);
		if (cn == 0)
		{
			cc = lha_get (&b, 9);
			memset (&ct, 0, sizeof (ct));
		}
		else
		{
			cc = -1;
			if (cn > 510)
				goto fail;
			uint i = 0;
			while (i < cn)
			{
				int c = pc >= 0 ? pc : lha_decode (&b, &pt);
				if (c < 0)
					goto fail;
				if (c <= 2)
				{
					uint z = c == 0 ? 1 : c == 1 ? lha_get (&b, 4) + 3 : lha_get (&b, 9) + 20;
					while (z-- && i < 510)
						clen[i++] = 0;
				}
				else
					clen[i++] = c - 2;
			}
			lha_build (&ct, clen, 510);
		}
		if (!lha_read_pt (&b, np, pbit, -1, &dt, &dc))
			goto fail;
		for (uint k = 0; k < blk && o < osize; k++)
		{
			int c = cc >= 0 ? cc : lha_decode (&b, &ct);
			if (c < 0)
				goto fail;
			if (c < 256)
				out[o++] = c;
			else
			{
				const uint len = c - 256 + 3;
				int j = dc >= 0 ? dc : lha_decode (&b, &dt);
				if (j < 0)
					goto fail;
				size_t dist = j;
				if (j > 1)
					dist = ((size_t)1 << (j - 1)) + lha_get (&b, j - 1);
				dist++;
				if (dist > o || dist > win)
					goto fail;
				for (uint m = 0; m < len && o < osize; m++, o++)
					out[o] = out[o - dist];
			}
		}
	}
	if (o == osize)
		return out;
fail:
	FREE (out);
	return 0;
}

static enumError extract_lha_file (ccp arg, ccp basedir, uint depth)
{
	if ((!is_ext (arg, ".lzh") && !is_ext (arg, ".lha")) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	uint count = 0;
	bool first = true;
	for (size_t p = 0; p + 22 < sz && raw[p];)
	{
		const u8 *h = raw + p;
		if (h[2] != '-' || h[6] != '-')
			break;
		const uint level = h[20];
		if (level > 2)
			break;
		size_t hsize, pack, orig;
		pack = h[7] | h[8] << 8 | h[9] << 16 | (size_t)h[10] << 24;
		orig = h[11] | h[12] << 8 | h[13] << 16 | (size_t)h[14] << 24;
		char name[PATH_MAX] = "", dir[PATH_MAX] = "";
		size_t data;
		if (level == 2)
		{
			hsize = h[0] | h[1] << 8;
			data = p + hsize;
			// extension headers follow the fixed 24 byte part
			size_t e = p + 24;
			while (e + 3 <= p + hsize)
			{
				const uint es = raw[e] | raw[e + 1] << 8;
				if (es < 3 || e + es > p + hsize)
					break;
				if (raw[e + 2] == 1)
					snprintf (name, sizeof (name), "%.*s", (int)(es - 3), raw + e + 3);
				else if (raw[e + 2] == 2)
				{
					snprintf (dir, sizeof (dir), "%.*s", (int)(es - 3), raw + e + 3);
					for (char *c = dir; *c; c++)
						if (*c == (char)0xff || *c == '\\') *c = '/';
				}
				e += es;
			}
		}
		else
		{
			hsize = h[0] + 2;
			const uint nl = h[21];
			if (22 + nl > h[0] + 2 || p + 22 + nl > sz)
				break;
			snprintf (name, sizeof (name), "%.*s", (int)nl, h + 22);
			data = p + hsize;
			if (level == 1)
			{
				// skip the extension headers (they are counted in the packed size)
				size_t e = p + hsize;
				size_t ext = 0;
				while (e + 2 <= sz)
				{
					const uint es = raw[e] | raw[e + 1] << 8;
					if (!es)
					{
						e += 2;
						break;
					}
					if (e + es > sz)
						break;
					if (raw[e + 2] == 2)
					{
						snprintf (dir, sizeof (dir), "%.*s", (int)(es - 3), raw + e + 3);
						for (char *c = dir; *c; c++)
							if (*c == (char)0xff || *c == '\\') *c = '/';
					}
					ext += es;
					e += es;
				}
				data = e;
				pack = pack >= ext ? pack - ext : 0;
			}
		}
		if (data + pack > sz)
			break;
		first = false;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s%s", dir, name);
		for (char *c = path; *c; c++)
			if (*c == '\\') *c = '/';
		const u8 *m = h + 2;
		if (!memcmp (m, "-lhd-", 5) || !*name)
			;
		else if (unixarch_safe (path))
		{
			u8 *out = 0;
			bool freeout = false;
			if (!memcmp (m, "-lh0-", 5) && orig == pack)
				out = (u8 *)raw + data;
			else if (!memcmp (m, "-lh5-", 5) || !memcmp (m, "-lh6-", 5) || !memcmp (m, "-lh7-", 5))
			{
				out = lha_unpack (raw + data, pack, orig, m[3] == '5' ? 13 : m[3] == '6' ? 15 : 16);
				freeout = out != 0;
			}
			if (out)
			{
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sEXTRACT LHA:%s -> %s/%s%s (%zu bytes)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, basedir ? basedir : "", path, orig);
				err = shade_write (arg, dest, basedir, path, out, (uint)orig);
				count++;
				if (freeout)
					FREE (out);
			}
			else
				fprintf (stdlog, "LHA: skipping %s (method %.5s unsupported or data damaged)\n", path, m);
		}
		p = data + pack;
		if (err != ERR_OK && err != ERR_NOTHING_TO_DO)
			break;
	}
	(void)first;
	if (!count && err == ERR_NOTHING_TO_DO)
		;
	else if (count && err == ERR_NOTHING_TO_DO)
		err = ERR_OK;
	FREE (raw);
	return err;
}
