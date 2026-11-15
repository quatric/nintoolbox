// Pikmin 1 ".blo" screen layouts (P2DScreen::makeHiearachyPanes in the decompilation): a stream of u16 pane types
// (0 end of level, 1 begin children {u16}, 2 end {u16}, 0x10 pane, 0x11 window, 0x12 picture, 0x13 text box), each pane
// being {u8 visible, u8, char tag[4], s16 x, y, w, h} plus its type specific fields (resource names, colours, text).
// Written as a flat pane list with parent links to "<name>.blo.json".
typedef struct
{
	pcr_ctx_t c;
	uint n_panes;
	int depth;
} blo_ctx_t;

static void blo_str (blo_ctx_t *b, const u8 *s, uint len)
{
	pcr_emit (&b->c, "\"");
	for (uint i = 0; i < len && s[i]; i++)
	{
		if (s[i] == '"' || s[i] == '\\')
			pcr_emit (&b->c, "\\%c", s[i]);
		else if (s[i] < 0x20 || s[i] >= 0x7f)
			pcr_emit (&b->c, "\\u%04x", s[i]);
		else
			pcr_emit (&b->c, "%c", s[i]);
	}
	pcr_emit (&b->c, "\"");
}

// TIMG / TLUT / FONT resource {u8 kind, u8 size, name}
static bool blo_res (blo_ctx_t *b, const char *key)
{
	pcr_ctx_t *c = &b->c;
	if (!pcr_need (c, 2))
		return false;
	const uint kind = c->d[c->p], len = c->d[c->p + 1];
	c->p += 2;
	if (len && (kind != 0 && kind != 2))
		return c->ok = false;
	if (!pcr_need (c, len))
		return false;
	pcr_emit (c, ", \"%s\": ", key);
	if (len)
		blo_str (b, c->d + c->p, len);
	else
		pcr_emit (c, "null");
	c->p += len;
	return true;
}

static bool blo_colour (blo_ctx_t *b, const char *key)
{
	pcr_ctx_t *c = &b->c;
	if (!pcr_need (c, 4))
		return false;
	pcr_emit (c, ", \"%s\": [%u, %u, %u, %u]", key, c->d[c->p], c->d[c->p + 1], c->d[c->p + 2], c->d[c->p + 3]);
	c->p += 4;
	return true;
}

static bool blo_level (blo_ctx_t *b, int parent)
{
	pcr_ctx_t *c = &b->c;
	int cur = parent;
	if (++b->depth > 64)
		return c->ok = false;
	for (;;)
	{
		if (!pcr_need (c, 2))
			return false;
		const uint type = rd_be16 (c->d + c->p);
		c->p += 2;
		if (type == 0)
			break;
		if (type == 1 || type == 2)
		{
			if (!pcr_need (c, 2))
				return false;
			c->p += 2;
			if (type == 2)
				break;
			if (!blo_level (b, cur))
				return false;
			continue;
		}
		if (type < 0x10 || type > 0x13 || !pcr_need (c, 14))
			return c->ok = false;
		const u8 *h = c->d + c->p;
		cur = (int)b->n_panes++;
		pcr_emit (c, "%s\n    {\"id\": %d, \"parent\": %d, \"type\": \"%s\", \"visible\": %u, \"tag\": ", cur ? "," : "", cur,
			parent, type == 0x10 ? "pane" : type == 0x11 ? "window" : type == 0x12 ? "picture" : "textbox", h[0]);
		blo_str (b, h + 2, 4);
		pcr_emit (c, ", \"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d", (int)(int16_t)rd_be16 (h + 6),
			(int)(int16_t)rd_be16 (h + 8), (int)(int16_t)rd_be16 (h + 10), (int)(int16_t)rd_be16 (h + 12));
		c->p += 14;
		if (type == 0x12)
		{
			if (!blo_res (b, "texture") || !blo_res (b, "palette") || !pcr_need (c, 2))
				return false;
			pcr_emit (c, ", \"binding\": %u, \"mirror\": %u, \"tumble\": %u, \"wrap\": %u", c->d[c->p], c->d[c->p + 1] & 3,
				c->d[c->p + 1] >> 2 & 1, c->d[c->p + 1] >> 3 & 3);
			c->p += 2;
		}
		else if (type == 0x11)
		{
			if (!pcr_need (c, 8))
				return false;
			pcr_emit (c, ", \"windowX\": %d, \"windowY\": %d, \"windowW\": %d, \"windowH\": %d",
				(int)(int16_t)rd_be16 (c->d + c->p), (int)(int16_t)rd_be16 (c->d + c->p + 2),
				(int)(int16_t)rd_be16 (c->d + c->p + 4), (int)(int16_t)rd_be16 (c->d + c->p + 6));
			c->p += 8;
			if (!blo_res (b, "topLeftTexture") || !blo_res (b, "topRightTexture") || !blo_res (b, "bottomLeftTexture")
				|| !blo_res (b, "bottomRightTexture") || !blo_res (b, "palette") || !pcr_need (c, 1))
				return false;
			pcr_emit (c, ", \"windowFlag\": %u", c->d[c->p++]);
			if (!blo_colour (b, "topLeftColour") || !blo_colour (b, "topRightColour")
				|| !blo_colour (b, "bottomLeftColour") || !blo_colour (b, "bottomRightColour"))
				return false;
		}
		else if (type == 0x13)
		{
			if (!blo_res (b, "font") || !blo_colour (b, "charColour") || !blo_colour (b, "gradColour")
				|| !pcr_need (c, 2))
				return false;
			const uint hb = c->d[c->p], vb = c->d[c->p + 1];
			c->p += 2;
			pcr_emit (c, ", \"hBinding\": %u, \"vBinding\": %u", hb & ~0x80u, vb);
			if (!(hb & 0x80) || !pcr_need (c, 8))
				return c->ok = false;
			pcr_emit (c, ", \"spacing\": %d, \"leading\": %d, \"fontWidth\": %d, \"fontHeight\": %d",
				(int)(int16_t)rd_be16 (c->d + c->p), (int)(int16_t)rd_be16 (c->d + c->p + 2),
				(int)(int16_t)rd_be16 (c->d + c->p + 4), (int)(int16_t)rd_be16 (c->d + c->p + 6));
			c->p += 8;
			if (!pcr_need (c, 2))
				return false;
			const uint tl = rd_be16 (c->d + c->p);
			c->p += 2;
			if (!pcr_need (c, tl))
				return false;
			pcr_emit (c, ", \"text\": ");
			blo_str (b, c->d + c->p, tl);
			c->p += tl;
		}
		if (type != 0x10)
			c->p = (c->p + 3) & ~(size_t)3;
		pcr_emit (c, "}");
		c->first = false;
	}
	b->depth--;
	return c->ok;
}

static enumError extract_pikmin_blo_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".blo"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	blo_ctx_t b;
	memset (&b, 0, sizeof (b));
	b.c.d = raw;
	b.c.sz = sz;
	b.c.ok = sz >= 16 && rd_be16 (raw) == 0x10;
	enumError err = ERR_NOTHING_TO_DO;
	if (b.c.ok)
	{
		pcr_emit (&b.c, "{\n  \"panes\": [");
		if (blo_level (&b, -1) && b.c.ok)
		{
			while (b.c.p < sz && !raw[b.c.p])
				b.c.p++;
			if (b.c.p == sz)
			{
				pcr_emit (&b.c, "\n  ]\n}\n");
				char dest[PATH_MAX];
				snprintf (dest, sizeof (dest), "%s.json", arg);
				if (testmode)
				{
					fprintf (stdlog, "WOULD EXTRACT BLO:%s -> JSON:%s\n", arg, dest);
					err = ERR_OK;
				}
				else
				{
					File_t F;
					err = CreateFileOpt (&F, true, dest, false, arg);
					if (F.f && fwrite (b.c.out, 1, b.c.len, F.f) != b.c.len)
						err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)b.c.len, dest);
					ResetFile (&F, opt_preserve);
					if (verbose >= 0 && !err)
						fprintf (stdlog, "%sEXTRACT BLO:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
				}
			}
		}
	}
	FREE (b.c.out);
	FREE (raw);
	return err;
}
