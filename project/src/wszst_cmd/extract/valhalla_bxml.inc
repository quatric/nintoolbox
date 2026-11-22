// Valhalla Knights: Eldar Saga (Wii) "*.bxml" binary XML (header version 3, flags 0x0804), big endian:
//   u16 version 3, u16 header size (0x10, or 0x210 in the localised ITEMPARAM / LAYOUT / QUEST files), u16 flags 0x0804,
//   u16 element count N, u16 attribute entry count M, u16 0, u16 0, u16 string pool size, then
//   N element records {u16 first child, u16 child count, u16 first attribute entry, u16 attribute count} (8 bytes),
//   M attribute entries {u16 name offset, u16 value offset} (4 bytes) and the NUL terminated string pool.
// Element 0 is the root and the children of an element are the consecutive records first child .. first child + count - 1
// (the records are laid out per subtree, not in plain breadth first order). The entry at "first attribute entry" carries the
// element name (value 0xffff, or the offset of the element's text when it has one); the "attribute count" entries after it are the
// attributes (a value offset of 0xffff means an empty value). The file size is exactly 16 + 8 * N + 4 * M + pool size. Written as "<file>.xml"; strings are
// Shift-JIS in some files (names such as the doorway labels), so the XML declares Shift_JIS when the pool is not valid UTF-8 and the bytes are copied as they are. Text that is an
// XML comment ("<!--...-->", kept as the text of the next element by the original tool) is written back as a comment.
// The sibling header versions (2: 12 byte records, 1: 16 byte records with 32 bit attribute entries, flags 0x0808 in
// enparam.bxml) are not handled.
typedef struct
{
	const u8 *pool;
	uint psz;
	const u8 *nd;
	const u8 *at;
	uint n, m;
	pcr_ctx_t *cx;
	u8 *seen;
	bool ok;
} vkbx_t;

static ccp vkbx_str (vkbx_t *v, uint off)
{
	if (off == 0xffff) // attribute without a value
		return "";
	if (off >= v->psz || !memchr (v->pool + off, 0, v->psz - off))
	{
		v->ok = false;
		return "";
	}
	return (ccp)v->pool + off;
}

static void vkbx_text (vkbx_t *v, ccp s, bool attr)
{
	for (; *s; s++)
		switch (*s)
		{
			case '&': pcr_emit (v->cx, "&amp;"); break;
			case '<': pcr_emit (v->cx, "&lt;"); break;
			case '>': pcr_emit (v->cx, "&gt;"); break;
			case '"':
				if (attr)
					pcr_emit (v->cx, "&quot;");
				else
					pcr_emit (v->cx, "\"");
				break;
			default: pcr_emit (v->cx, "%c", *s);
		}
}

static void vkbx_node (vkbx_t *v, uint i, uint depth)
{
	if (!v->ok || i >= v->n || depth > 200 || v->seen[i])
	{
		v->ok = false;
		return;
	}
	v->seen[i] = 1;
	const u8 *r = v->nd + (size_t)i * 8;
	const uint first = rd_be16 (r), cnt = rd_be16 (r + 2), as = rd_be16 (r + 4), ac = rd_be16 (r + 6);
	if ((u64)first + cnt > v->n || (u64)as + 1 + ac > v->m)
	{
		v->ok = false;
		return;
	}
	const u8 *e = v->at + (size_t)as * 4;
	ccp name = vkbx_str (v, rd_be16 (e));
	const uint tv = rd_be16 (e + 2);
	for (uint k = 0; k < depth; k++)
		pcr_emit (v->cx, "\t");
	pcr_emit (v->cx, "<%s", name);
	for (uint k = 1; k <= ac; k++)
	{
		const u8 *a = v->at + (size_t)(as + k) * 4;
		pcr_emit (v->cx, " %s=\"", vkbx_str (v, rd_be16 (a)));
		vkbx_text (v, vkbx_str (v, rd_be16 (a + 2)), true);
		pcr_emit (v->cx, "\"");
	}
	ccp text = tv != 0xffff ? vkbx_str (v, tv) : "";
	while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
		text++;
	if (!cnt && !*text)
	{
		pcr_emit (v->cx, "/>\n");
		return;
	}
	pcr_emit (v->cx, ">");
	if (*text)
	{
		// comments of the source XML survive as element text: write them back as comments
		const size_t tl = strlen (text);
		if (tl >= 7 && !strncmp (text, "<!--", 4) && !strcmp (text + tl - 3, "-->"))
			for (ccp q = text; *q; q++)
				pcr_emit (v->cx, "%c", *q);
		else
			vkbx_text (v, text, false);
	}
	if (cnt)
	{
		pcr_emit (v->cx, "\n");
		for (uint k = 0; k < cnt; k++)
			vkbx_node (v, first + k, depth + 1);
		for (uint k = 0; k < depth; k++)
			pcr_emit (v->cx, "\t");
	}
	pcr_emit (v->cx, "</%s>\n", name);
}

static bool vkbx_is_utf8 (const u8 *d, uint n)
{
	for (uint i = 0; i < n;)
	{
		const u8 c = d[i];
		const uint k = c < 0x80 ? 0 : (c & 0xe0) == 0xc0 && c >= 0xc2 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 && c <= 0xf4 ? 3 : 99;
		if (k == 99 || i + k >= n)
			return false;
		for (uint j = 1; j <= k; j++)
			if ((d[i + j] & 0xc0) != 0x80)
				return false;
		i += 1 + k;
	}
	return true;
}

static enumError extract_valhalla_bxml_file (ccp arg, ccp basedir, uint depth)
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
	{
		// the last pool string may run up to the end of the file without a terminator: work on a copy with a spare NUL
		u8 *cp = CALLOC (sz + 1, 1);
		if (!cp)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		memcpy (cp, raw, sz);
		FREE (raw);
		raw = cp;
	}
	if (sz < 0x10 || rd_be16 (raw) != 3 || (rd_be16 (raw + 2) != 0x10 && rd_be16 (raw + 2) != 0x210) || rd_be16 (raw + 4) != 0x804
		|| rd_be16 (raw + 10) || rd_be16 (raw + 12))
		goto done;
	{
		vkbx_t v;
		memset (&v, 0, sizeof (v));
		v.n = rd_be16 (raw + 6);
		v.m = rd_be16 (raw + 8);
		v.psz = rd_be16 (raw + 14);
		const uint psz_file = v.psz;
		if (!v.n || (u64)16 + (u64)v.n * 8 + (u64)v.m * 4 + psz_file != sz)
			goto done;
		v.nd = raw + 16;
		v.at = v.nd + (size_t)v.n * 8;
		v.pool = v.at + (size_t)v.m * 4;
		v.psz = psz_file + 1; // includes the spare NUL behind the file
		v.ok = true;
		v.seen = CALLOC (v.n, 1);
		pcr_ctx_t cx;
		memset (&cx, 0, sizeof (cx));
		cx.d = raw;
		cx.sz = sz;
		cx.ok = true;
		v.cx = &cx;
		if (v.seen)
		{
			pcr_emit (&cx, "<?xml version=\"1.0\" encoding=\"%s\"?>\n", vkbx_is_utf8 (v.pool, psz_file) ? "UTF-8" : "Shift_JIS");
			vkbx_node (&v, 0, 0);
			for (uint i = 0; v.ok && i < v.n; i++)
				if (!v.seen[i])
					v.ok = false;
		}
		if (v.seen && v.ok && cx.ok)
		{
			char dest[PATH_MAX];
			snprintf (dest, sizeof (dest), "%s.xml", arg);
			if (testmode)
			{
				fprintf (stdlog, "WOULD EXTRACT VALHALLA_BXML:%s -> XML:%s\n", arg, dest);
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
					fprintf (stdlog, "%sEXTRACT VALHALLA_BXML:%s -> XML:%s\n", verbose > 0 ? "\n" : "", arg, dest);
			}
		}
		FREE (cx.out);
		FREE (v.seen);
	}
done:
	FREE (raw);
	return err;
}
