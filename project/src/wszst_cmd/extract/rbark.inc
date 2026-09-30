// Harmonix "Ark" archives (Guitar Hero, Rock Band series): main.hdr plus
// main_0.ark, main_1.ark, ... beside it. The parts can be several GB, so
// members are streamed from the part files straight to disk rather than
// loaded into memory; see lib-rbark.h for the header format.

typedef struct rbark_order_t
{
	const rbark_t *a;
	uint i;
} rbark_order_t;

static int rbark_cmp (const void *pa, const void *pb)
{
	const rbark_order_t *x = pa, *y = pb;
	const int c = strcasecmp (x->a->e[x->i].path, y->a->e[y->i].path);
	return c ? c : (x->i < y->i ? -1 : x->i > y->i);
}

static enumError extract_rbark_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".hdr"))
		return ERR_NOTHING_TO_DO;

	// header files are small; anything huge is not an Ark header
	FILE *hf = fopen (arg, "rb");
	if (!hf)
		return ERR_NOTHING_TO_DO;
	fseek (hf, 0, SEEK_END);
	const long hsize = ftell (hf);
	rewind (hf);
	if (hsize < 16 || hsize > 0x4000000)
	{
		fclose (hf);
		return ERR_NOTHING_TO_DO;
	}
	u8 *hdr = MALLOC ((size_t)hsize);
	const bool got = hdr && fread (hdr, 1, (size_t)hsize, hf) == (size_t)hsize;
	fclose (hf);
	if (!got)
	{
		FREE (hdr);
		return ERR_NOTHING_TO_DO;
	}

	rbark_t a;
	const enumError perr = RbArkParse (&a, hdr, (size_t)hsize);
	FREE (hdr);
	if (perr)
		return ERR_NOTHING_TO_DO;

	// part files: <hdr stem>_<i>.ark beside the header
	char stem[PATH_MAX];
	snprintf (stem, sizeof (stem), "%s", arg);
	char *dot = strrchr (stem, '.');
	if (dot)
		*dot = 0;
	FILE *part[RBARK_MAX_PARTS] = { 0 };
	uint have = 0;
	for (uint i = 0; i < a.n_parts; i++)
	{
		char pp[PATH_MAX];
		snprintf (pp, sizeof (pp), "%s_%u.ark", stem, i);
		part[i] = fopen (pp, "rb");
		have += part[i] != 0;
	}
	if (!have)
	{
		RbArkFree (&a);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ARK v%u:%s (%u entries, %u/%u parts) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", a.version, arg, a.n, have, a.n_parts,
			dest);

	// case-insensitive duplicate detection
	rbark_order_t *ord = MALLOC (sizeof (*ord) * a.n);
	uint *dup = CALLOC (a.n, sizeof (uint));
	if (ord && dup)
	{
		for (uint i = 0; i < a.n; i++)
			ord[i] = (rbark_order_t){ &a, i };
		qsort (ord, a.n, sizeof (*ord), rbark_cmp);
		for (uint k = 1, run = 0; k < a.n; k++)
		{
			if (!strcasecmp (a.e[ord[k].i].path, a.e[ord[k - 1].i].path))
				dup[ord[k].i] = ++run + 1;
			else
				run = 0;
		}
	}

	enumError err = ERR_OK;
	uint skipped = 0;
	for (uint i = 0; !err && i < a.n && !testmode; i++)
	{
		const rbark_entry_t *e = a.e + i;
		uint pi;
		u64 local;
		if (!RbArkLocate (&a, e->offset, &pi, &local) || !part[pi])
		{
			skipped++;
			continue;
		}
		char name[PATH_MAX];
		ccp src = valid_sarc_path (e->path) ? e->path : 0;
		if (!src)
			snprintf (name, sizeof (name), "%05u.bin", i);
		else if (dup[i])
		{
			ccp slash = strrchr (src, '/');
			ccp dt = strrchr (src, '.');
			const int stem_len = (dt && (!slash || dt > slash)) ? (int)(dt - src) : (int)strlen (src);
			snprintf (name, sizeof (name), "%.*s_dup%u%s", stem_len, src, dup[i],
				(dt && (!slash || dt > slash)) ? dt : "");
		}
		else
			snprintf (name, sizeof (name), "%s", src);

		u8 *buf = e->size ? MALLOC (e->size) : 0;
		if (e->size && (!buf || fseeko (part[pi], (off_t)local, SEEK_SET)
							  || fread (buf, 1, e->size, part[pi]) != e->size))
		{
			FREE (buf);
			skipped++;
			continue;
		}
		const u8 *out = buf;
		uint out_size = e->size;
		u8 *inflated = 0;
		if (e->inflated && e->inflated != e->size && buf)
		{
			// the stored size differs from the inflated one: zlib member
			inflated = MALLOC (e->inflated);
			uLongf dl = e->inflated;
			if (inflated && uncompress (inflated, &dl, buf, e->size) == Z_OK && dl == e->inflated)
			{
				out = inflated;
				out_size = e->inflated;
			}
		}

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && out_size && fwrite (out, 1, out_size, F.f) != out_size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", out_size, path);
		ResetFile (&F, opt_preserve);
		FREE (inflated);
		FREE (buf);
	}
	if (skipped && verbose >= 0)
		fprintf (stdlog, "ARK: %u member(s) skipped (missing part file or short read)\n", skipped);

	for (uint i = 0; i < a.n_parts; i++)
		if (part[i])
			fclose (part[i]);
	FREE (ord);
	FREE (dup);
	RbArkFree (&a);

	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}
