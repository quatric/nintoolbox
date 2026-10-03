// Radical "ATG CORE CEMENT LIBRARY" archives (Crash of the Titans, Wii;
// *.rcf). Members are streamed from the archive to disk rather than held in
// memory (the largest is 718 MB); see lib-rcf.h.
typedef struct rcf_order_t
{
	const rcf_t *r;
	uint i;
} rcf_order_t;

static int rcf_cmp_name (const void *pa, const void *pb)
{
	const rcf_order_t *x = pa, *y = pb;
	ccp a = x->r->e[x->i].name ? x->r->e[x->i].name : "", b = y->r->e[y->i].name ? y->r->e[y->i].name : "";
	const int c = strcasecmp (a, b);
	return c ? c : (x->i < y->i ? -1 : x->i > y->i);
}

static enumError extract_rcf_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".rcf"))
		return ERR_NOTHING_TO_DO;

	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 first[0x40];
	if (fread (first, 1, sizeof (first), f) != sizeof (first))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	const size_t head_size = RcfHeadSize (first, sizeof (first));
	if (!head_size)
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	fseeko (f, 0, SEEK_END);
	const u64 file_size = (u64)ftello (f);
	rewind (f);

	u8 *head = MALLOC (head_size);
	rcf_t r;
	if (!head || fread (head, 1, head_size, f) != head_size || RcfParse (&r, head, head_size, file_size))
	{
		FREE (head);
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	FREE (head);

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT RCF:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, r.n, dest);

	// duplicate detection over the final names
	rcf_order_t *ord = MALLOC (sizeof (*ord) * r.n);
	uint *dup = CALLOC (r.n, sizeof (uint));
	if (!ord || !dup)
	{
		FREE (ord);
		FREE (dup);
		RcfFree (&r);
		fclose (f);
		return ERR_OUT_OF_MEMORY;
	}
	{
		for (uint i = 0; i < r.n; i++)
			ord[i] = (rcf_order_t){ &r, i };
		qsort (ord, r.n, sizeof (*ord), rcf_cmp_name);
		for (uint k = 1, run = 0; k < r.n; k++)
		{
			ccp a = r.e[ord[k].i].name, b = r.e[ord[k - 1].i].name;
			if (a && b && !strcasecmp (a, b))
				dup[ord[k].i] = ++run + 1;
			else
				run = 0;
		}
	}

	enumError err = ERR_OK;
	for (uint i = 0; !err && i < r.n && !testmode; i++)
	{
		const rcf_entry_t *e = r.e + i;
		char name[PATH_MAX];
		ccp src = e->name && valid_sarc_path (e->name) ? e->name : 0;
		if (!src)
			snprintf (name, sizeof (name), "%08x.bin", e->hash);
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

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (!err && F.f && e->size && !fseeko (f, (off_t)e->offset, SEEK_SET))
		{
			u8 *buf = MALLOC (1 << 20);
			u32 left = e->size;
			if (!buf)
				err = ERR_OUT_OF_MEMORY;
			while (buf && left)
			{
				const size_t n = fread (buf, 1, left < (1 << 20) ? left : (1 << 20), f);
				if (!n || fwrite (buf, 1, n, F.f) != n)
				{
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", e->size, path);
					break;
				}
				left -= (u32)n;
			}
			FREE (buf);
		}
		else if (!err && F.f && e->size)
			err = ERR_READ_FAILED;
		const enumError close_err = ResetFile (&F, opt_preserve);
		if (close_err > err)
			err = close_err;
	}

	fclose (f);
	FREE (ord);
	FREE (dup);
	RcfFree (&r);

	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}
