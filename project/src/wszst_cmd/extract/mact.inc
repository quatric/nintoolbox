// Petz "MACT" action tables (Petz Sports, Wii; 3d/animation/*_mact, no extension), big-endian: {"MACT", u32 name length, name,
// u32 N, N x u32 id hash, 3N + 1 link records {u8 kind, u8 flags, u32 hash}, 16 bytes, N x {u16 flags, s16 a, s16 b} (-1 = none),
// 16 bytes}. Every one of the 2314 retail files has exactly 12 + name length + 28 N + 38 bytes. What the kinds, flags and
// hashes stand for is not known (the hash function is unknown; with the sibling skin present the bone ids are resolved to node indices), so the file is written as "<name>.txt" with all records.
static enumError extract_mact_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!petz_has_magic (arg, "MACT"))
		return ERR_NOTHING_TO_DO;
	u8 *d = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &d, &sz, 0, 0, 0, false) || !d)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	const size_t nl = sz >= 12 ? rd_be32 (d + 4) : 0;
	const size_t n = sz >= 12 && 12 + nl <= sz ? rd_be32 (d + 8 + nl) : 0;
	if (sz < 12 || nl > 256 || 12 + nl > sz || n > 100000 || sz != 12 + nl + 28 * n + 38)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PETZ-MACT:%s (%zu entries) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg,
			n, dest);
	// bone ids resolve to node indices through the node table of the sibling "<name>" skin (arg without "_mact")
	u32 nodeh[256];
	uint nnode = 0;
	{
		char skin[PATH_MAX];
		const size_t al = strlen (arg);
		u8 *sd = 0;
		size_t ssz = 0;
		if (al > 5 && al < sizeof (skin) && !strcmp (arg + al - 5, "_mact"))
		{
			snprintf (skin, sizeof (skin), "%.*s", (int)(al - 5), arg);
			uint cnt = 0;
			if (!LoadFileAlloc (skin, 0, 0, &sd, &ssz, 0, 0, 0, false) && sd)
			{
				const size_t b = petz_skel_find (sd, ssz, &cnt);
				for (uint i = 0; b && i < cnt && i < 256; i++)
					nodeh[nnode++] = rd_be32 (sd + b + 88 * (size_t)i);
			}
			FREE (sd);
		}
	}
#define MACT_NODE(h, out) \
	do { (out)[0] = 0; for (uint q_ = 0; q_ < nnode; q_++) if (nodeh[q_] == (h)) snprintf (out, sizeof (out), " node %u", q_); } while (0)
	enumError err = ERR_OK;
	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f)
		{
			FILE *f = F.f;
			const u8 *p = d + 12 + nl;
			fprintf (f, "# Petz action table, name \"%.*s\", %zu entries\n# id hashes\n", (int)nl, d + 8, n);
			for (size_t i = 0; i < n; i++)
			{
				char nd[24];
				MACT_NODE (rd_be32 (p + 4 * i), nd);
				fprintf (f, "%zu %08x%s\n", i, rd_be32 (p + 4 * i), nd);
			}
			p += 4 * n;
			fprintf (f, "# links: kind flags hash\n");
			for (size_t i = 0; i < 3 * n + 1; i++)
			{
				char nd[24];
				MACT_NODE (rd_be32 (p + 6 * i + 2), nd);
				fprintf (f, "%zu %u %u %08x%s\n", i, p[6 * i], p[6 * i + 1], rd_be32 (p + 6 * i + 2), nd);
			}
			p += 6 * (3 * n + 1);
			fprintf (f, "# block 1:");
			for (int i = 0; i < 16; i++)
				fprintf (f, " %02x", p[i]);
			p += 16;
			fprintf (f, "\n# entries: flags a b\n");
			for (size_t i = 0; i < n; i++)
				fprintf (f, "%zu 0x%04x %d %d\n", i, rd_be16 (p + 6 * i), (int)(s16)rd_be16 (p + 6 * i + 2),
					(int)(s16)rd_be16 (p + 6 * i + 4));
			p += 6 * n;
			fprintf (f, "# block 2:");
			for (int i = 0; i < 16; i++)
				fprintf (f, " %02x", p[i]);
			fputc ('\n', f);
		}
		const enumError ce = ResetFile (&F, opt_preserve);
		err = err ? err : ce;
	}
	FREE (d);
	return err;
}
