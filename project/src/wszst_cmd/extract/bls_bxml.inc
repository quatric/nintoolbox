// Big League Sports / World Championship Sports (Wii) "*.bxml" binary XML, big endian:
//   u32 version 1, u32 string table size S, u32 element count N, u32 attribute count M, the string table, N element records (16 bytes)
//   and M attribute records (12 bytes); the file size is exactly 16 + S + 16 * N + 12 * M.
// String table entries are {u32 name hash (not used), NUL terminated string, padded so that the string takes ((len + 1) & ~3) + 4 bytes}.
// Every pointer is a signed 32 bit offset relative to the position of the word that holds it. An element record is
//   {name -> entry start, next sibling (relative to the record, 0 = none), first child, first attribute (0 = none)},
// an attribute record {name -> entry start, value -> string start, next attribute (relative to the record, 0 = none)}.
// The first record is the first top level element; its siblings are further top level elements (files with an <Include> in
// front of <BLS> have several). Written as "<file>.xml" (a fragment when there is more than one top level element).
typedef struct
{
	const u8 *d;
	size_t sz;
	uint ssz, n, m;
	u8 *ent, *seen_e, *seen_a;
	vkbx_t x;
	bool ok;
} blsbx_t;

static ccp blsbx_str (blsbx_t *b, size_t p, bool value)
{
	const s64 tg = (s64)p + (int)rd_be32 (b->d + p);
	if (tg < 16 + (value ? 4 : 0) || tg >= 16 + (s64)b->ssz || !b->ent[tg - 16 - (value ? 4 : 0)])
	{
		b->ok = false;
		return "";
	}
	ccp s = (ccp)b->d + tg + (value ? 0 : 4);
	if (!value)
		for (ccp q = s; *q; q++)
			if (!isalnum ((u8)*q) && *q != '_' && *q != '.' && *q != '-' && *q != ':')
			{
				b->ok = false;
				return "";
			}
	return s;
}

static void blsbx_chain (blsbx_t *b, size_t r, uint depth);

static void blsbx_elem (blsbx_t *b, size_t r, uint depth)
{
	const size_t t = 16 + (size_t)b->ssz, ea = t + (size_t)b->n * 16;
	if (!b->ok || depth > 200 || r < t || r >= ea || (r - t) % 16 || b->seen_e[(r - t) / 16])
	{
		b->ok = false;
		return;
	}
	b->seen_e[(r - t) / 16] = 1;
	ccp name = blsbx_str (b, r, false);
	const int ch = (int)rd_be32 (b->d + r + 8), at = (int)rd_be32 (b->d + r + 12);
	pcr_ctx_t *cx = b->x.cx;
	for (uint k = 0; k < depth; k++)
		pcr_emit (cx, "\t");
	pcr_emit (cx, "<%s", name);
	for (s64 a = at ? (s64)r + at : 0; a && b->ok;)
	{
		if (a < (s64)ea || a + 12 > (s64)b->sz || (a - (s64)ea) % 12 || b->seen_a[(a - (s64)ea) / 12])
		{
			b->ok = false;
			break;
		}
		b->seen_a[(a - (s64)ea) / 12] = 1;
		pcr_emit (cx, " %s=\"", blsbx_str (b, a, false));
		vkbx_text (&b->x, blsbx_str (b, a + 4, true), true);
		pcr_emit (cx, "\"");
		const int nx = (int)rd_be32 (b->d + a + 8);
		a = nx ? a + nx : 0;
	}
	if (!ch)
	{
		pcr_emit (cx, "/>\n");
		return;
	}
	pcr_emit (cx, ">\n");
	blsbx_chain (b, (size_t)((s64)r + ch), depth + 1);
	for (uint k = 0; k < depth; k++)
		pcr_emit (cx, "\t");
	pcr_emit (cx, "</%s>\n", name);
}

static void blsbx_chain (blsbx_t *b, size_t r, uint depth)
{
	for (;;)
	{
		blsbx_elem (b, r, depth);
		const int sb = b->ok ? (int)rd_be32 (b->d + r + 4) : 0;
		if (!sb)
			return;
		r = (size_t)((s64)r + sb);
	}
}

static enumError extract_bls_bxml_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".bxml"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	blsbx_t b;
	memset (&b, 0, sizeof (b));
	pcr_ctx_t cx;
	memset (&cx, 0, sizeof (cx));
	cx.ok = true;
	if (sz < 0x20 || rd_be32 (raw) != 1)
		goto done;
	b.d = raw;
	b.sz = sz;
	b.ssz = rd_be32 (raw + 4);
	b.n = rd_be32 (raw + 8);
	b.m = rd_be32 (raw + 12);
	if (!b.n || (u64)16 + b.ssz + (u64)b.n * 16 + (u64)b.m * 12 != sz)
		goto done;
	b.ent = CALLOC ((size_t)b.ssz + 1, 1);
	b.seen_e = CALLOC (b.n, 1);
	b.seen_a = CALLOC ((size_t)b.m + 1, 1);
	if (!b.ent || !b.seen_e || !b.seen_a)
		goto done;
	{
		size_t o = 16;
		const size_t end = 16 + (size_t)b.ssz;
		while (o < end)
		{
			const size_t e = o, st = o + 4;
			ccp z = st < end ? memchr (raw + st, 0, end - st) : 0;
			if (!z)
				goto done;
			b.ent[e - 16] = 1;
			o = st + (((size_t)((u8 *)z - raw - st) + 1) & ~(size_t)3) + 4;
		}
		if (o != end)
			goto done;
	}
	b.ok = true;
	b.x.cx = &cx;
	blsbx_chain (&b, 16 + (size_t)b.ssz, 0);
	for (uint i = 0; b.ok && i < b.n; i++)
		b.ok = b.seen_e[i];
	for (uint i = 0; b.ok && i < b.m; i++)
		b.ok = b.seen_a[i];
	if (b.ok && cx.ok && cx.len)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.xml", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT BLS_BXML:%s -> XML:%s\n", arg, dest);
			err = ERR_OK;
		}
		else
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f && fwrite (cx.out, 1, cx.len, F.f) != cx.len)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)cx.len, dest);
			ResetFile (&F, opt_preserve);
			if (verbose >= 0 && !err)
				fprintf (stdlog, "%sEXTRACT BLS_BXML:%s -> XML:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
done:
	FREE (cx.out);
	FREE (b.ent);
	FREE (b.seen_e);
	FREE (b.seen_a);
	FREE (raw);
	return err;
}
