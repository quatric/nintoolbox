// Voxler "*.vxla" karaoke annotation files (Let's Sing, The Voice, Sing 4, ...): plain XML, ISO-8859-1,
//   <AnnotationFile><IntervalLayer name="..."><Interval t1="sec" t2="sec" value="..."/>...
// Layers used here: "lyrics_cut" (one syllable per interval), "notes" (UINT8 pitch; a syllable is exported when a note has exactly the same interval) and "pages"
// (display pages; a new line starts when the page changes). The other layers (lyrics, notes_golden, preview, ...) are ignored.
// Written as an UltraStar song text "<file>.txt" (no audio tag; BPM 1200 = 80 beats per second, pitch = the notes value, not
// transposed, golden notes not marked).
typedef struct
{
	double t1, t2;
	char *v;
} vxiv_t;

static uint vxla_layer (ccp xml, ccp name, vxiv_t **out)
{
	char pat[0x60];
	snprintf (pat, sizeof (pat), "name=\"%s\"", name);
	ccp p = strstr (xml, pat), end = p ? strstr (p, "</IntervalLayer>") : 0;
	uint n = 0, cap = 0;
	*out = 0;
	while (p && end && (p = strstr (p, "<Interval ")) && p < end)
	{
		ccp e = strchr (p, '>'), a = strstr (p, "t1=\""), b = strstr (p, "t2=\""), c = strstr (p, "value=\"");
		if (!e || !a || a > e || !b || b > e || !c || c > e)
			break;
		if (n == cap)
		{
			cap = cap ? cap * 2 : 64;
			vxiv_t *no = REALLOC (*out, cap * sizeof (**out));
			if (!no)
				break;
			*out = no;
		}
		vxiv_t *iv = *out + n++;
		iv->t1 = strtod (a + 4, 0);
		iv->t2 = strtod (b + 4, 0);
		c += 7;
		ccp q = strchr (c, '"');
		iv->v = MALLOC ((q ? (size_t)(q - c) : 0) + 1);
		char *d = iv->v;
		for (; iv->v && q && c < q; c++)
		{
			static const struct { ccp s; char c; } ent[] = { { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' } };
			uint k = 0;
			for (; k < 5 && strncmp (c, ent[k].s, strlen (ent[k].s)); k++)
				;
			if (k < 5)
			{
				*d++ = ent[k].c;
				c += strlen (ent[k].s) - 1;
			}
			else
				*d++ = *c;
		}
		if (d)
			*d = 0;
		p = e;
	}
	return n;
}

static void vxla_free (vxiv_t *v, uint n)
{
	for (uint i = 0; i < n; i++)
		FREE (v[i].v);
	FREE (v);
}

static enumError extract_vxla_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".vxla"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	char *xml = MALLOC (sz + 1);
	vxiv_t *ly = 0, *nt = 0, *pg = 0;
	uint nl = 0, nn = 0, np = 0, shown = 0;
	pcr_ctx_t cx;
	memset (&cx, 0, sizeof (cx));
	cx.ok = true;
	if (!xml)
		goto done;
	memcpy (xml, raw, sz);
	xml[sz] = 0;
	if (!strstr (xml, "<AnnotationFile"))
		goto done;
	nl = vxla_layer (xml, "lyrics_cut", &ly);
	nn = vxla_layer (xml, "notes", &nt);
	np = vxla_layer (xml, "pages", &pg);
	if (!nl || !nn)
		goto done;
	{
		char name[PATH_MAX];
		ccp slash = strrchr (arg, '/'), base = slash ? slash + 1 : arg;
		snprintf (name, sizeof (name), "%s", base);
		*strrchr (name, '.') = 0;
		pcr_emit (&cx, "#TITLE:%s\n#BPM:1200\n#GAP:0\n#ENCODING:CP1252\n", name);
		uint page = 0, last_page = ~0u;
		long prev_end = 0;
		uint j = 0;
		for (uint i = 0; i < nl; i++)
		{
			// syllables without a note of identical timing (spoken parts) are left out
			while (j < nn && nt[j].t1 < ly[i].t1)
				j++;
			if (j == nn || nt[j].t1 != ly[i].t1 || nt[j].t2 != ly[i].t2 || !ly[i].v || !nt[j].v)
				continue;
			while (page + 1 < np && pg[page].t2 <= ly[i].t1)
				page++;
			const long s = lround (ly[i].t1 * 80), e = lround (ly[i].t2 * 80);
			if (shown++ && page != last_page)
				pcr_emit (&cx, "- %ld\n", prev_end);
			last_page = page;
			pcr_emit (&cx, ": %ld %ld %ld ", s, e > s ? e - s : 1, strtol (nt[j].v, 0, 10));
			for (ccp q = ly[i].v; *q; q++)
				pcr_emit (&cx, "%c", *q);
			pcr_emit (&cx, "\n");
			prev_end = e;
		}
		pcr_emit (&cx, "E\n");
	}
	if (cx.ok && shown)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.txt", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT VXLA:%s -> TXT:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT VXLA:%s -> TXT:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
done:
	vxla_free (ly, nl);
	vxla_free (nt, nn);
	vxla_free (pg, np);
	FREE (cx.out);
	FREE (xml);
	FREE (raw);
	return err;
}
