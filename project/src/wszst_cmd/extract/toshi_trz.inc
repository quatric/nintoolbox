// Blue Tongue Toshi property trees (".trz" and the "Main"-only ".trb" files, Nickelodeon Barnyard Wii): a TSFB container whose decompressed section holds a PBProperties tree (OpenBarnyard
// Toshi/Source/Plugins/PPropertyParser/PBProperties.h). All pointers are section offsets (big endian): PBProperties {parent, PBProperty *props, u32 count}; PBProperty {name ptr,
// value ptr} (8 bytes each); the name is a pointer to a PBPropertyName {char *}, or directly the NUL-terminated string; PBPropertyValue {u32 type (0 int, 1 u32, 2 float, 3 bool,
// 4 properties, 5 property name, 6 array, 7 string, 8 locale string index), u32 value}; PBPropertyValueArray {PBPropertyValue *values, u32 count}. Written as a YAML-style listing
// "<file>.yaml" for the symbol that parses as a property tree.
typedef struct
{
	const toshi_trb_t *t;
	FILE *f;
	uint nodes;
	bool bad;
} trz_ctx_t;

static bool trz_ok (const trz_ctx_t *c, u64 off, u64 len)
{
	return off <= c->t->sect_size && len <= c->t->sect_size - off;
}

static ccp trz_str (const trz_ctx_t *c, u32 off)
{
	if (!trz_ok (c, off, 1))
		return 0;
	size_t n = 0;
	while (off + n < c->t->sect_size && c->t->sect[off + n])
		n++;
	if (off + n >= c->t->sect_size || n > 4096)
		return 0;
	for (size_t i = 0; i < n; i++)
		if ((u8)c->t->sect[off + i] < 0x20 && c->t->sect[off + i] != '\t')
			return 0;
	return (ccp)c->t->sect + off;
}

static ccp trz_name (const trz_ctx_t *c, u32 off)
{
	if (trz_ok (c, off, 4))
	{
		ccp s = trz_str (c, rd_be32 (c->t->sect + off));
		if (s)
			return s;
	}
	return trz_str (c, off);
}

static void trz_value (trz_ctx_t *c, const u8 *v, uint depth);

static void trz_props (trz_ctx_t *c, u32 off, uint depth)
{
	if (c->bad || depth > 40 || ++c->nodes > 2000000 || !trz_ok (c, off, 12))
	{
		c->bad = true;
		return;
	}
	const u32 po = rd_be32 (c->t->sect + off + 4), n = rd_be32 (c->t->sect + off + 8);
	if (n > 100000 || (n && !trz_ok (c, po, 8ull * n)))
	{
		c->bad = true;
		return;
	}
	for (u32 i = 0; i < n && !c->bad; i++)
	{
		const u32 no = rd_be32 (c->t->sect + po + 8 * i), vo = rd_be32 (c->t->sect + po + 8 * i + 4);
		ccp nm = trz_name (c, no);
		if (!nm || !trz_ok (c, vo, 8))
		{
			c->bad = true;
			return;
		}
		for (uint k = 0; k < depth; k++)
			fputs ("  ", c->f);
		fprintf (c->f, "%s:", nm);
		trz_value (c, c->t->sect + vo, depth);
	}
}

static void trz_value (trz_ctx_t *c, const u8 *v, uint depth)
{
	const u32 ty = rd_be32 (v), x = rd_be32 (v + 4);
	switch (ty)
	{
	case 0:
		fprintf (c->f, " %d\n", (int)x);
		break;
	case 1:
		fprintf (c->f, " %u\n", x);
		break;
	case 2:
	{
		float f;
		memcpy (&f, &x, 4);
		fprintf (c->f, " %.9g\n", f);
		break;
	}
	case 3:
		fprintf (c->f, " %s\n", x ? "true" : "false");
		break;
	case 4:
		fputc ('\n', c->f);
		trz_props (c, x, depth + 1);
		break;
	case 5:
	{
		ccp s = trz_name (c, x);
		fprintf (c->f, " @%s\n", s ? s : "?");
		break;
	}
	case 6:
	{
		if (!trz_ok (c, x, 8))
		{
			c->bad = true;
			return;
		}
		const u32 vo = rd_be32 (c->t->sect + x), n = rd_be32 (c->t->sect + x + 4);
		if (n > 100000 || (n && !trz_ok (c, vo, 8ull * n)))
		{
			c->bad = true;
			return;
		}
		fputs (" [", c->f);
		for (u32 i = 0; i < n && !c->bad; i++)
		{
			const u8 *e = c->t->sect + vo + 8 * i;
			const u32 et = rd_be32 (e), ex = rd_be32 (e + 4);
			if (i)
				fputs (", ", c->f);
			if (et == 0)
				fprintf (c->f, "%d", (int)ex);
			else if (et == 1 || et == 8)
				fprintf (c->f, "%u", ex);
			else if (et == 2)
			{
				float f;
				memcpy (&f, &ex, 4);
				fprintf (c->f, "%.9g", f);
			}
			else if (et == 3)
				fputs (ex ? "true" : "false", c->f);
			else if (et == 7)
			{
				ccp s = trz_str (c, ex);
				fprintf (c->f, "\"%s\"", s ? s : "?");
			}
			else
				fprintf (c->f, "<type %u>", et);
		}
		fputs ("]\n", c->f);
		break;
	}
	case 7:
	{
		ccp s = trz_str (c, x);
		if (!s)
			c->bad = true;
		fprintf (c->f, " \"%s\"\n", s ? s : "?");
		break;
	}
	case 8:
		fprintf (c->f, " locale#%u\n", x);
		break;
	default:
		c->bad = true;
	}
}

static enumError extract_toshi_trz_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	const bool trb_ext = is_ext (arg, ".trb");
	if (!trb_ext && !is_ext (arg, ".trz"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	toshi_trb_t trb;
	if (OpenToshiTrb (&trb, raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	// plain .trb files only qualify when their "Main" symbol is a property tree (the others are models, terrain, sound tables ...)
	const s64 mainsym = ToshiSymbol (&trb, "Main");
	if (trb_ext && mainsym < 0)
	{
		ResetToshiTrb (&trb);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 ns = trb.symb_size >= 4 ? rd_be32 (trb.symb) : 0;
	for (u32 i = 0; i < ns && i < 4096 && err == ERR_NOTHING_TO_DO; i++)
	{
		if (4 + 12 * (size_t)(i + 1) > trb.symb_size)
			break;
		const u8 *e = trb.symb + 4 + 12 * i;
		const u32 off = rd_be32 (e + 8);
		if (trb_ext && (s64)off != mainsym)
			continue;
		// dry run into the null device to validate, then write for real
		FILE *tf = fopen ("/dev/null", "w");
		if (!tf)
			break;
		trz_ctx_t c = { &trb, tf, 0, false };
		trz_props (&c, off, 0);
		fclose (tf);
		if (c.bad || !c.nodes)
			continue;
		char dest[PATH_MAX];
		if (opt_dest)
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".yaml", false);
		else
			snprintf (dest, sizeof (dest), "%s.yaml", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT TOSHI-TRZ:%s (%u properties) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, c.nodes, dest);
		err = ERR_OK;
		if (!testmode)
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f)
			{
				fprintf (F.f, "# Blue Tongue Toshi property tree, decoded from %s\n", arg);
				c.f = F.f;
				c.nodes = 0;
				trz_props (&c, off, 0);
				if (ferror (F.f))
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing YAML failed: %s\n", dest);
			}
			ResetFile (&F, opt_preserve);
		}
	}
	ResetToshiTrb (&trb);
	FREE (raw);
	return err;
}
